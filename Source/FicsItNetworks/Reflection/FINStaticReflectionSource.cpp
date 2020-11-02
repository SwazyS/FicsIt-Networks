﻿#include "FINStaticReflectionSource.h"

#include "FGBuildableDockingStation.h"
#include "FGBuildableFactory.h"
#include "FGBuildableManufacturer.h"
#include "FGBuildablePipeReservoir.h"
#include "FGBuildableRailroadSignal.h"
#include "FGBuildableRailroadStation.h"
#include "FGBuildableRailroadSwitchControl.h"
#include "FGBuildableTrainPlatform.h"
#include "FGBuildableTrainPlatformCargo.h"
#include "FGFactoryConnectionComponent.h"
#include "FGHealthComponent.h"
#include "FGPipeSubsystem.h"
#include "FGPowerCircuit.h"
#include "FINArrayProperty.h"
#include "FINFuncProperty.h"
#include "FINGlobalRegisterHelper.h"
#include "FINIntProperty.h"
#include "FINObjectProperty.h"
#include "FINStructProperty.h"

#include "FGPowerConnectionComponent.h"
#include "FGPowerInfoComponent.h"
#include "FGRailroadTimeTable.h"
#include "FGRailroadVehicleMovementComponent.h"
#include "FGTargetPointLinkedList.h"
#include "FGTrainStationIdentifier.h"
#include "FINBoolProperty.h"
#include "FINClassProperty.h"
#include "FINFloatProperty.h"
#include "FINTraceProperty.h"
#include "FicsItKernel/Processor/Lua/LuaInstance.h"
#include "FicsItKernel/Processor/Lua/LuaLib.h"
#include "Network/FINNetworkConnectionComponent.h"
#include "util/ReflectionHelper.h"
#include "Utils/FINTimeTableStop.h"
#include "Utils/FINTrackGraph.h"

TMap<UClass*, FFINStaticClassReg> UFINStaticReflectionSource::Classes;

void UFINStaticReflectionSource::AddClass(UClass* Class, const FString& InternalName, const FText& DisplayName, const FText& Description) {
	FFINStaticClassReg& Reg = Classes.FindOrAdd(Class);
	Reg.InternalName = InternalName;
	Reg.DisplayName = DisplayName;
	Reg.Description = Description;
}

void UFINStaticReflectionSource::AddFunction(UClass* Class, int FuncID, const FString& InternalName, const FText& DisplayName, const FText& Description, bool VarArgs, const TFunction<void(const FINTrace&, TArray<FINAny>&)>& Func, int Runtime, int FuncType) {
	FFINStaticFuncReg& Reg = Classes.FindOrAdd(Class).Functions.FindOrAdd(FuncID);
	Reg.InternalName = InternalName;
	Reg.DisplayName = DisplayName;
	Reg.Description = Description;
	Reg.VarArgs = VarArgs;
	Reg.Function = Func;
	Reg.Runtime = Runtime;
	Reg.FuncType = FuncType;
}

bool UFINStaticReflectionSource::ProvidesRequirements(UClass* Class) const {
	return Classes.Contains(Class);
}

void UFINStaticReflectionSource::FillData(FFINReflection* Ref, UFINClass* ToFillClass, UClass* Class) const {
	const FFINStaticClassReg* ClassReg = Classes.Find(Class);
	if (!ClassReg) return;
	ToFillClass->InternalName = ClassReg->InternalName;
	ToFillClass->DisplayName = ClassReg->DisplayName;
	ToFillClass->Description = ClassReg->Description;

	for (const TPair<int, FFINStaticFuncReg>& KVFunc : ClassReg->Functions) {
		const FFINStaticFuncReg& Func = KVFunc.Value;
		UFINFunction* FINFunc = NewObject<UFINFunction>(ToFillClass);
		FINFunc->InternalName = Func.InternalName;
		FINFunc->DisplayName = Func.DisplayName;
		FINFunc->Description = Func.Description;
		if (Func.VarArgs) FINFunc->FunctionFlags = FINFunc->FunctionFlags | FIN_Func_VarArgs;
		switch (Func.Runtime) {
		case 0:
			FINFunc->FunctionFlags = (FINFunc->FunctionFlags & ~FIN_Func_Runtime) | FIN_Func_Sync;
			break;
		case 1:
			FINFunc->FunctionFlags = (FINFunc->FunctionFlags & ~FIN_Func_Runtime) | FIN_Func_Parallel;
			break;
		case 2:
			FINFunc->FunctionFlags = (FINFunc->FunctionFlags & ~FIN_Func_Runtime) | FIN_Func_Async;
			break;
		default:
			break;
		}
		switch (Func.FuncType) {
		case 1:
			FINFunc->FunctionFlags = FINFunc->FunctionFlags | FIN_Func_ClassFunc;
			break;
		case 2:
			FINFunc->FunctionFlags = FINFunc->FunctionFlags | FIN_Func_StaticFunc;
			break;
		default:
			break;
		}

		TArray<int> ParamPos;
		Func.Parameters.GetKeys(ParamPos);
		ParamPos.Sort();
		for (int Pos : ParamPos) {
			const FFINStaticFuncParamReg& Param = Func.Parameters[Pos];
			UFINProperty* FINProp = Param.PropConstructor(FINFunc);
			FINProp->InternalName = Param.InternalName;
			FINProp->DisplayName = Param.DisplayName;
			FINProp->Description = Param.Description;
			FINProp->PropertyFlags = FINProp->PropertyFlags | FIN_Prop_Param;
			switch (Param.ParamType) {
				case 2:
					FINProp->PropertyFlags = FINProp->PropertyFlags | FIN_Prop_RetVal;
				case 1:
					FINProp->PropertyFlags = FINProp->PropertyFlags | FIN_Prop_OutParam;
					break;
				default: break;
			}
			FINFunc->Parameters.Add(FINProp);
		}
		ToFillClass->Functions.Add(FINFunc);
	}

	for (const TPair<int, FFINStaticPropReg>& KVProp : ClassReg->Properties) {
		const FFINStaticPropReg& Prop = KVProp.Value;
		UFINProperty* FINProp = Prop.PropConstructor(ToFillClass);
		FINProp->InternalName = Prop.InternalName;
		FINProp->DisplayName = Prop.DisplayName;
		FINProp->Description = Prop.Description;
		FINProp->PropertyFlags = FINProp->PropertyFlags | FIN_Prop_Attrib;
		if (UFINFuncProperty* FINFuncProp = Cast<UFINFuncProperty>(FINProp)) {
			FINFuncProp->GetterFunc.GetterFunc = Prop.Get;
			if ((bool)Prop.Set) FINFuncProp->SetterFunc.SetterFunc = Prop.Set;
			else FINProp->PropertyFlags = FINProp->PropertyFlags | FIN_Prop_ReadOnly;
		}
		switch (Prop.Runtime) {
		case 0:
			FINProp->PropertyFlags = (FINProp->PropertyFlags & ~FIN_Prop_Runtime) | FIN_Prop_Sync;
			break;
		case 1:
			FINProp->PropertyFlags = (FINProp->PropertyFlags & ~FIN_Prop_Runtime) | FIN_Prop_Parallel;
			break;
		case 2:
			FINProp->PropertyFlags = (FINProp->PropertyFlags & ~FIN_Prop_Runtime) | FIN_Prop_Async;
			break;
		default:
			break;
		}
		ToFillClass->Properties.Add(FINProp);
	}
}

#define TypeClassName(Type) FIN_StaticRef_ ## Type
#define BeginType(Type, InternalName, DisplayName, Description) \
	namespace TypeClassName(Type) { \
		using T = Type; \
		FFINStaticGlobalRegisterFunc RegClass([](){ \
			UFINStaticReflectionSource::AddClass(T::StaticClass(), InternalName, DisplayName, Description); \
		});
#define EndType() };
#define GetClassFunc [](){ return T::StaticClass(); }
#define FuncClassName(Prefix, Func) FIN_StaticRefFunc_ ## Prefix ## _ ## Func
#define BeginFuncRT(Prefix, InternalName, DisplayName, Description, Varargs, FuncType, Runtime) \
	namespace FuncClassName(Prefix, InternalName) { \
		const int F = __COUNTER__; \
		void Execute(const FINTrace& Ctx, TArray<FINAny>& Params); \
		FFINStaticGlobalRegisterFunc RegClass([](){ \
			UFINStaticReflectionSource::AddFunction(T::StaticClass(), F, #InternalName, DisplayName, Description, Varargs, &Execute, Runtime, FuncType); \
			TArray<FINAny> Params; \
			Execute(FINTrace(nullptr), Params); \
		}); \
		void Execute(const FINTrace& Ctx, TArray<FINAny>& Params) { \
		static bool _bGotReg = false;
#define GET_MACRO(_0, VAL,...) VAL
#define BeginFunc(InternalName, DisplayName, Description, ...) BeginFuncRT(Member, InternalName, DisplayName, Description, false, 0, GET_MACRO(0 , ##__VA_ARGS__, 1) ) \
		T* self = Cast<T>(*Ctx);
#define BeginFuncVA(InternalName, DisplayName, Description, ...) BeginFuncRT(Member, InternalName, DisplayName, Description, true, 0, GET_MACRO(0, ##__VA_ARGS__, 1) ) \
		T* self = Cast<T>(*Ctx);
#define BeginClassFunc(InternalName, DisplayName, Description, VA, ...) BeginFuncRT(Class, InternalName, DisplayName, Description, VA, 1, GET_MACRO(0, ##__VA_ARGS__, 1) ) \
		TSubclassOf<T> self = Cast<UClass>(*Ctx);
#define BeginStaticFunc(InternalName, DisplayName, Description, VA, ...) BeginFuncRT(Static, InternalName, DisplayName, Description, VA, 2, GET_MACRO(0, ##__VA_ARGS__, 1) ) \ 
		TSubclassOf<T> self = Cast<UClass>(*Ctx);
#define Body() \
			if (self && _bGotReg) {
#define EndFunc() \
			else if (!_bGotReg) _bGotReg = true; \
			} \
		} \
	};
#define PropClassName(Prefix, Prop) FIN_StaticRefProp_ ## Prefix ## _ ## Prop
#define BeginPropRT(Prefix, Type, InternalName, DisplayName, Description, Runtime) \
	namespace PropClassName(Prefix, InternalName) { \
		const int P = __COUNTER__; \
		using PT = Type; \
		FINAny Get(void* Ctx); \
		FFINStaticGlobalRegisterFunc RegProp([](){ \
			UFINStaticReflectionSource::AddProp<Type>(T::StaticClass(), P, #InternalName, DisplayName, Description, &Get, Runtime); \
		}); \
		FINAny Get(void* Ctx) {
#define BeginProp(Type, InternalName, DisplayName, Description, ...) BeginPropRT(Member, Type, InternalName, DisplayName, Description, GET_MACRO(0, ##__VA_ARGS__, 1) ) \
	T* self = Cast<T>(static_cast<UObject*>(Ctx));
#define BeginClassProp(Type, InternalName, DisplayName, Description, ...) BeginPropRT(Class, Type, InternalName, DisplayName, Description, GET_MACRO(0, ##__VA_ARGS__, 1) ) \
	TSubclassOf<T> self = Cast<UClass>(static_cast<UObject*>(Ctx));
#define BeginStaticProp(Type, InternalName, DisplayName, Description, ...) BeginPropRT(Static, Type, InternalName, DisplayName, Description, GET_MACRO(0, ##__VA_ARGS__, 1) )
#define Return \
		return (FINAny)
#define PropSet() \
		} \
		void Set(void* Ctx, const FINAny& Val); \
		FFINStaticGlobalRegisterFunc RegPropSet([](){ \
			UFINStaticReflectionSource::AddPropSetter(T::StaticClass(), P, &Set); \
		}); \
		void Set(void* Ctx, const FINAny& AnyVal) { \
			T* self = Cast<T>(static_cast<UObject*>(Ctx)); \
			PT::CppType Val = PT::Get(AnyVal);
#define EndProp() \
		} \
	};

#define InVal(Pos, Type, InternalName, DisplayName, Description) \
Type::CppType InternalName = Type::CppType(); \
if (!_bGotReg) { UFINStaticReflectionSource::AddFuncParam<Type>(T::StaticClass(), F, Pos, #InternalName, DisplayName, Description, 0);  } \
else InternalName = Type::Get(Params[Pos]);
#define OutVal(Pos, Type, InternalName, DisplayName, Description) \
FINAny& InternalName = _bGotReg ? Params[Pos] : *(FINAny*)nullptr; \
if (!_bGotReg) { UFINStaticReflectionSource::AddFuncParam<Type>(T::StaticClass(), F, Pos, #InternalName, DisplayName, Description, 1); }
#define RetVal(Pos, Type, InternalName, DisplayName, Description) \
FINAny& InternalName = _bGotReg ? Params[Pos] : *(FINAny*)nullptr; \
if (!_bGotReg) { UFINStaticReflectionSource::AddFuncParam<Type>(T::StaticClass(), F, Pos, #InternalName, DisplayName, Description, 3); }

#define TFS(Str) FText::FromString( Str )

struct RInt {
	typedef FINInt CppType;
	static FINInt Get(const FFINAnyNetworkValue& Any) { return Any.GetInt(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		return NewObject<UFINIntProperty>(Outer);
	}
};

struct RFloat {
	typedef FINFloat CppType;
	static FINFloat Get(const FINAny& Any) { return Any.GetFloat(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		return NewObject<UFINFloatProperty>(Outer);
	}
};

struct RBool {
	typedef FINBool CppType;
	static FINBool Get(const FINAny& Any) { return Any.GetBool(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		return NewObject<UFINBoolProperty>(Outer);
	}
};

struct RString {
	typedef FINStr CppType;
	static FINStr Get(const FINAny& Any) { return Any.GetString(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		return NewObject<UFINStructProperty>(Outer);
	}
};

struct RClass {
	typedef FINClass CppType;
	static FINClass Get(const FINAny& Any) { return Any.GetClass(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		return NewObject<UFINClassProperty>(Outer);
	}
};

struct RObject {
	typedef FINObj CppType;
	static FINObj Get(const FINAny& Any) { return Any.GetObject(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		return NewObject<UFINObjectProperty>(Outer);
	}
};

struct RTrace {
	typedef FINTrace CppType;
	static FINTrace Get(const FINAny& Any) { return Any.GetTrace(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		return NewObject<UFINTraceProperty>(Outer);
	}
};
 
template<typename T>
struct RStruct {
	typedef T CppType;
	static T Get(const FINAny& Any) { return Any.GetStruct().Get<T>(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		UFINStructProperty* FINProp = NewObject<UFINStructProperty>(Outer);
		FINProp->Struct = TBaseStructure<T>::Get();
		return FINProp;
	}
};

template<typename T>
struct RArray {
	typedef FINArray CppType;
	static FINArray Get(const FINAny& Any) { return Any.GetArray(); }
	static UFINProperty* PropConstructor(UObject* Outer) {
		UFINArrayProperty* FINProp = NewObject<UFINArrayProperty>(Outer);
		FINProp->InnerType = T::PropConstructor(FINProp);
		return FINProp;
	}
};

BeginType(UObject, "Object", TFS("Object"), TFS("The base class of every object."))
BeginProp(RInt, hash, TFS("Hash"), TFS("A Hash of this object. This is a value that nearly uniquely identifies this object.")) {
	Return (int64)GetTypeHash(self);
} EndProp()
BeginFunc(getHash, TFS("Get Hash"), TFS("Returns a hash of this object. This is a value that nearly uniquely identifies this object.")) {
	OutVal(0, RInt, hash, TFS("Hash"), TFS("The hash of this object."));
	Body()
	hash = (int64)GetTypeHash(self);
} EndFunc()
BeginClassProp(RInt, hash, TFS("Hash"), TFS("A Hash of this object. This is a value that nearly uniquely identifies this object.")) {
	Return (int64)GetTypeHash(self);
} EndProp()
BeginClassFunc(getHash, TFS("Get Hash"), TFS("Returns the hash of this class. This is a value that nearly uniquely idenfies this object."), false) {
	OutVal(0, RInt, hash, TFS("Hash"), TFS("The hash of this class."));
	Body()
	hash = (int64) GetTypeHash(self);
} EndFunc()
EndType()

BeginType(AActor, "Actor", TFS("Actor"), TFS("This is the base class of all things that can exist within the world by them self."))
BeginProp(RStruct<FVector>, location, TFS("Location"), TFS("The location of the actor in the world.")) {
	Return self->GetActorLocation();
} EndProp()
BeginProp(RStruct<FVector>, scale, TFS("Scale"), TFS("The scale of the actor in the world.")) {
	Return self->GetActorScale();
} EndProp()
BeginProp(RStruct<FRotator>, rotation, TFS("Rotation"), TFS("The rotation of the actor in the world.")) {
	Return self->GetActorRotation();
} EndProp()
BeginFunc(getPowerConnectors, TFS("Get Power Connectors"), TFS("Returns a list of power connectors this actor might have.")) {
	OutVal(0, RArray<RTrace>, connectors, TFS("Connectors"), TFS("The power connectors this actor has."));
	Body()
	FINArray Output;
	const TSet<UActorComponent*>& Components = self->GetComponents();
	for (TFieldIterator<UObjectProperty> prop(self->GetClass()); prop; ++prop) {
		if (!prop->PropertyClass->IsChildOf(UFGPowerConnectionComponent::StaticClass())) continue;
		UObject* Connector = *prop->ContainerPtrToValuePtr<UObject*>(self);
		if (!Components.Contains(Cast<UActorComponent>(Connector))) continue;
		Output.Add(Ctx / Connector);
	}
	connectors = Output;
} EndFunc()
BeginFunc(getFactoryConnectors, TFS("Get Factory Connectors"), TFS("Returns a list of factory connectors this actor might have.")) {
	OutVal(0, RArray<RTrace>, connectors, TFS("Connectors"), TFS("The factory connectors this actor has."));
	Body()
	FINArray Output;
	const TSet<UActorComponent*>& Components = self->GetComponents();
	for (TFieldIterator<UObjectProperty> prop(self->GetClass()); prop; ++prop) {
		if (!prop->PropertyClass->IsChildOf(UFGFactoryConnectionComponent::StaticClass())) continue;
		UObject* Connector = *prop->ContainerPtrToValuePtr<UObject*>(self);
		if (!Components.Contains(Cast<UActorComponent>(Connector))) continue;
		Output.Add(Ctx / Connector);
	}
	connectors = Output;
} EndFunc()
BeginFunc(getInventories, TFS("Get Inventories"), TFS("Returns a list of inventories this actor might have.")) {
	OutVal(0, RArray<RTrace>, inventories, TFS("Inventories"), TFS("The inventories this actor has."));
	Body()
	FINArray Output;
	const TSet<UActorComponent*>& Components = self->GetComponents();
	for (TFieldIterator<UObjectProperty> prop(self->GetClass()); prop; ++prop) {
		if (!prop->PropertyClass->IsChildOf(UFGInventoryComponent::StaticClass())) continue;
		UObject* inventory = *prop->ContainerPtrToValuePtr<UObject*>(self);
		if (!Components.Contains(Cast<UActorComponent>(inventory))) continue;
		Output.Add(Ctx / inventory);
	}
	inventories = Output;
} EndFunc()
BeginFunc(getNetworkConnectors, TFS("Get Network Connectors"), TFS("Returns the name of network connectors this actor might have.")) {
	OutVal(0, RArray<RTrace>, connectors, TFS("Connectors"), TFS("The factory connectors this actor has."));
	Body()
	FINArray Output;
	const TSet<UActorComponent*>& Components = self->GetComponents();
	for (TFieldIterator<UObjectProperty> prop(self->GetClass()); prop; ++prop) {
		if (!prop->PropertyClass->IsChildOf(UFINNetworkConnectionComponent::StaticClass())) continue;
		UObject* connector = *prop->ContainerPtrToValuePtr<UObject*>(self);
		if (!Components.Contains(Cast<UActorComponent>(connector))) continue;
		Output.Add(Ctx / connector);
	}
	connectors = Output;
} EndFunc()
EndType()

BeginType(UFGInventoryComponent, "Inventory", TFS("Inventory"), TFS("A actor component that can hold multiple item stacks."))
BeginFuncVA(getStack, TFS("Get Stack"), TFS("Returns the item stack at the given index.\nTakes integers as input and returns the corresponding stacks.")) {
	Body()
	int ArgNum = Params.Num();
	for (int i = 0; i < ArgNum; ++i) {
		const FINAny& Any = Params[i];
		FInventoryStack Stack;
		if (Any.GetType() != FIN_INT && !self->GetStackFromIndex(Any.GetInt(), Stack)) {
			Params.Add(FINAny());
		} else {
			Params.Add(FINAny(Stack));
		}
	}
} EndFunc()
BeginProp(RInt, itemCount, TFS("Item Count"), TFS("The absolute amount of items in the whole inventory.")) {
	Return (int64)self->GetNumItems(nullptr);
} EndProp()
BeginProp(RInt, size, TFS("Size"), TFS("The count of available item stack slots this inventory has.")) {
	Return (int64)self->GetSizeLinear();
} EndProp()
BeginFunc(sort, TFS("Sort"), TFS("Sorts the whole inventory. (like the middle mouse click into a inventory)")) {
	Body()
	self->SortInventory();
} EndFunc()
BeginFunc(flush, TFS("Flush"), TFS("Removes all discardable items from the inventory completely. They will be gone! No way to get them back!"), 0) {
	Body()
	TArray<FInventoryStack> stacks;
	self->GetInventoryStacks(stacks);
	self->Empty();
	for (const FInventoryStack& stack : stacks) {
		if (stack.HasItems() && stack.Item.IsValid() && !UFGItemDescriptor::CanBeDiscarded(stack.Item.ItemClass)) {
			self->AddStack(stack);
		}
	}
} EndFunc()
EndType()

BeginType(UFGPowerConnectionComponent, "PowerConnection", TFS("Power Connection"), TFS("A actor component that allows for a connection point to the power network. Basically a point were a power cable can get attached to."))
BeginProp(RInt, connections, TFS("Connections"), TFS("The amount of connections this power connection has.")) {
	Return (int64)self->GetNumConnections();
} EndProp()
BeginProp(RInt, maxConnections, TFS("Max Connections"), TFS("The maximum amount of connections this power connection can handle.")) {
	Return (int64)self->GetMaxNumConnections();
} EndProp()
BeginFunc(getPower, TFS("Get Power"), TFS("Returns the power info component of this power connection.")) {
	OutVal(0, RTrace, power, TFS("Power"), TFS("The power info compoent this power connection uses."))
	Body()
	power = Ctx / self->GetPowerInfo();
} EndFunc();
BeginFunc(getCircuit, TFS("Get Circuit"), TFS("Returns the power circuit to which this connection component is attached to.")) {
	OutVal(0, RTrace, circuit, TFS("Circuit"), TFS("The Power Circuit this connection component is attached to."))
	Body()
	circuit = Ctx / self->GetPowerCircuit();
} EndFunc()
EndType()

BeginType(UFGPowerInfoComponent, "PowerInfo", TFS("Power Info"), TFS("A actor component that provides information and mainly statistics about the power connection it is attached to."))
BeginProp(RFloat, dynProduction, TFS("Dynamic Production"), TFS("The production cpacity this connection provided last tick.")) {
	Return self->GetRegulatedDynamicProduction();
} EndProp()
BeginProp(RFloat, baseProduction, TFS("Base Production"), TFS("The base production capactiy this connection always provides.")) {
	Return self->GetBaseProduction();
} EndProp()
BeginProp(RFloat, maxDynProduction,	TFS("Max Dynamic Production"), TFS("The maximum production capactiy this connection could have provided to the circuit in the last tick.")) {
	Return self->GetDynamicProductionCapacity();
} EndProp()
BeginProp(RFloat, targetConsumption, TFS("Target Consumption"), TFS("The amount of energy the connection wanted to consume from the circuit in the last tick.")) {
	Return self->GetTargetConsumption();
} EndProp()
BeginProp(RFloat, consumption, TFS("Consumption"), TFS("The amount of energy the connection actually consumed in the last tick.")) {
	Return self->GetBaseProduction();
} EndProp();
BeginProp(RBool, hasPower, TFS("Has Power"), TFS("True if the connection has satisfied power values and counts as beeing powered. (True if it has power)")) {
	Return self->HasPower();
} EndProp();
BeginFunc(getCircuit, TFS("Get Circuit"), TFS("Returns the power circuit this info component is part of.")) {
	OutVal(0, RTrace, circuit, TFS("Circuit"), TFS("The Power Circuit this info component is attached to."))
	Body()
	circuit = Ctx / self->GetPowerCircuit();
}
EndFunc()
EndType()

BeginType(UFGPowerCircuit, "PowerCircuit", TFS("Power Circuit"), TFS("A Object that represents a whole power circuit."))
// TODO: Hook & Signals - LuaLibHook(UFGPowerCircuit, UFINPowerCircuitHook)
BeginProp(RFloat, production, TFS("Production"), TFS("The amount of power produced by the whole circuit in the last tick.")) {
	FPowerCircuitStats stats;
	self->GetStats(stats);
	Return stats.PowerProduced;
} EndProp()
BeginProp(RFloat, consumption, TFS("Consumption"), TFS("The power consumption of the whole circuit in thge last tick.")) {
	FPowerCircuitStats stats;
	self->GetStats(stats);
	Return stats.PowerConsumed;
} EndProp()
BeginProp(RFloat, capacity, TFS("Capacity"), TFS("The power capacity of the whole network in the last tick. (The max amount of power available in the last tick)")) {
	FPowerCircuitStats stats;
	self->GetStats(stats);
	Return stats.PowerProductionCapacity;
} EndProp()
BeginProp(RBool, isFuesed, TFS("Is Fuesed"), TFS("True if the fuse in the network triggered.")) {
	Return self->IsFuseTriggered();
} EndProp()
EndType()

BeginType(UFGFactoryConnectionComponent, "FactoryConnection", TFS("Factory Connection"), TFS("A actor component that is a connection point to which a conveyor or pipe can get attached to."))
// TODO: Hook & Signals - LuaLibHook(UFGFactoryConnectionComponent, UFINFactoryConnectorHook)
BeginProp(RInt, type, TFS("Type"), TFS("Returns the type of the connection. 0 = Conveyor, 1 = Pipe")) {
	Return (int64)self->GetConnector();
} EndProp()
BeginProp(RInt, direction, TFS("Direction"), TFS("The direction in which the items/fluids flow. 0 = Input, 1 = Output, 2 = Any, 3 = Used just as snap point")) {
	Return (int64)self->GetDirection();
} EndProp()
BeginProp(RBool, isConnected, TFS("Is Connected"), TFS("True if something is connected to this connection.")) {
	Return self->IsConnected();
} EndProp()
BeginFunc(getInventory, TFS("Get Inventory"), TFS("Returns the internal inventory of the connection component.")) {
	OutVal(0, RTrace, inventory, TFS("Inventory"), TFS("The internal inventory of the connection component."))
	Body()
	inventory = Ctx / self->GetInventory();
} EndFunc()
EndType()

BeginType(AFGBuildableFactory, "Factory", TFS("Factory"), TFS("The base class of most machines you can build."))
BeginProp(RFloat, progress, TFS("Progress"), TFS("The current production progress of the current production cycle.")) {
	Return self->GetProductionProgress();
} EndProp()
BeginProp(RFloat, powerConsumProducing,	TFS("Producing Power Consumption"), TFS("The power consumption when producing.")) {
	Return self->GetProducingPowerConsumption();
} EndProp()
BeginProp(RFloat, productivity,	TFS("Productivity"), TFS("The productivity of this factory.")) {
	Return self->GetProductivity();
} EndProp()
BeginProp(RFloat, cycleTime, TFS("Cycle Time"), TFS("The time that passes till one production cycle is finsihed.")) {
	Return self->GetProductionCycleTime();
} EndProp()
BeginProp(RFloat, maxPotential, TFS("Max Potential"), TFS("The maximum potential this factory can be set to.")) {
	Return self->GetMaxPossiblePotential();
} EndProp()
BeginProp(RFloat, minPotential, TFS("Min Potential"), TFS("The minimum potential this factory needs to be set to.")) {
	Return self->GetMinPotential();
} EndProp()
BeginProp(RBool, standby, TFS("Standby"), TFS("True if the factory is in standby.")) {
	Return self->IsProductionPaused();
} PropSet() {
	self->SetIsProductionPaused(Val);
} EndProp()
BeginProp(RFloat, potential, TFS("Potential"), TFS("The potential this factory is currently set to. (the overclock value)\n 0 = 0%, 1 = 100%")) {
	Return self->GetPendingPotential();
} PropSet() {
	float min = self->GetMinPotential();
	float max = self->GetMaxPossiblePotential();
	self->SetPendingPotential(FMath::Clamp((float)Val, self->GetMinPotential(), self->GetMaxPossiblePotential()));
} EndProp()
EndType()

BeginType(AFGBuildableManufacturer, "Manufacturer", TFS("Manufacturer"), TFS("The base class of every machine that uses a recipe to produce something automatically."))
BeginFunc(getRecipe, TFS("Get Recipe"), TFS("Returns the currently set recipe of the manufacturer.")) {
	OutVal(0, RClass, recipe, TFS("Recipe"), TFS("The currently set recipe."))
	Body()
	recipe = (UClass*)self->GetCurrentRecipe();
} EndFunc()
BeginFunc(getRecipes, TFS("Get Recipes"), TFS("Returns the list of recipes this manufacturer can get set to and process.")) {
	OutVal(0, RArray<RClass>, recipes, TFS("Recipes"), TFS("The list of avalible recipes."))
	Body()
	TArray<FINAny> OutRecipes;
	TArray<TSubclassOf<UFGRecipe>> Recipes;
	self->GetAvailableRecipes(Recipes);
	for (TSubclassOf<UFGRecipe> Recipe : Recipes) {
		OutRecipes.Add((FINAny)(UClass*)Recipe);
	}
	recipes = OutRecipes;
} EndFunc()
BeginFunc(setRecipe, TFS("Set Recipe"), TFS("Sets the currently producing recipe of this manufacturer."), 1) {
	InVal(0, RClass, recipe, TFS("Recipe"), TFS("The recipe this manufacturer should produce."))
	OutVal(1, RBool, gotSet, TFS("Got Set"), TFS("True if the current recipe got successfully set to the new recipe."))
	Body()
	TArray<TSubclassOf<UFGRecipe>> recipes;
	self->GetAvailableRecipes(recipes);
	if (recipes.Contains(recipe)) {
		TArray<FInventoryStack> stacks;
		self->GetInputInventory()->GetInventoryStacks(stacks);
		self->GetOutputInventory()->AddStacks(stacks);
		self->SetRecipe(recipe);
		gotSet = true;
	} else {
		gotSet = false;
	}
} EndFunc()
BeginFunc(getInputInv, TFS("Get Input Inventory"), TFS("Returns the input inventory of this manufacturer.")) {
	OutVal(0, RTrace, inventory, TFS("Inventory"), TFS("The input inventory of this manufacturer"))
	Body()
	inventory = Ctx / self->GetInputInventory();
} EndFunc()
BeginFunc(getOutputInv, TFS("Get Output Inventory"), TFS("Returns the output inventory of this manufacturer.")) {
	OutVal(0, RTrace, inventory, TFS("Inventory"), TFS("The output inventory of this manufacturer."))
	Body()
	inventory = Ctx / self->GetOutputInventory();
} EndFunc()
EndType()

BeginType(AFGVehicle, "Vehicle", TFS("Vehicle"), TFS("A base class for all vehciles."))
BeginProp(RFloat, health, TFS("Health"), TFS("The health of the vehicle.")) {
	Return self->GetHealthComponent()->GetCurrentHealth();
} EndProp()
BeginProp(RFloat, maxHealth, TFS("Max Health"), TFS("The maximum amount of health this vehicle can have.")) {
	Return self->GetHealthComponent()->GetMaxHealth();
} EndProp()
BeginProp(RBool, isSelfDriving, TFS("Is Self Driving"), TFS("True if the vehicle is currently self driving.")) {
	Return self->IsSelfDriving();
} PropSet() {
	FReflectionHelper::SetPropertyValue<UBoolProperty>(self, TEXT("mIsSelfDriving"), Val);
} EndProp()
EndType()

BeginType(AFGWheeledVehicle, "WheeledVehicle", TFS("Wheeled Vehicle"), TFS("The base class for all vehicles that used wheels for movement."))
BeginFunc(getFuelInv, TFS("Get Fuel Inventory"), TFS("Returns the inventory that contains the fuel of the vehicle.")) {
	OutVal(0, RTrace, inventory, TFS("Inventory"), TFS("The fuel inventory of the vehicle."))
	Body()
	inventory = Ctx / self->GetFuelInventory();
} EndFunc()
BeginFunc(getStorageInv, TFS("Get Storage Inventory"), TFS("Returns the inventory that contains the storage of the vehicle.")) {
	OutVal(0, RTrace, inventory, TFS("Inventory"), TFS("The storage inventory of the vehicle."))
	Body()
	inventory = Ctx / self->GetStorageInventory();
} EndFunc()
BeginFunc(isValidFuel, TFS("Is Valid Fuel"), TFS("Allows to check if the given item type is a valid fuel for this vehicle.")) {
	InVal(0, RClass, item, TFS("Item"), TFS("The item type you want to check."))
	OutVal(1, RBool, isValid, TFS("Is Valid"), TFS("True if the given item type is a valid fuel for this vehicle."))
	Body()
	isValid = self->IsValidFuel(item);
} EndFunc()

inline int TargetToIndex(AFGTargetPoint* Target, UFGTargetPointLinkedList* List) {
	AFGTargetPoint* CurrentTarget = nullptr;
	int i = 0;
	do {
		if (i) CurrentTarget = CurrentTarget->mNext;
		else CurrentTarget = List->GetFirstTarget();
		if (CurrentTarget == Target) return i;
		++i;
	} while (CurrentTarget && CurrentTarget != List->GetLastTarget());
	return -1;
}

inline AFGTargetPoint* IndexToTarget(int index, UFGTargetPointLinkedList* List) {
	if (index < 0) return nullptr;
	AFGTargetPoint* CurrentTarget = List->GetFirstTarget();
	for (int i = 0; i < index && CurrentTarget; ++i) {
		CurrentTarget = CurrentTarget->mNext;
	}
	return CurrentTarget;
}

BeginFunc(getCurrentTarget, TFS("Get Current Target"), TFS("Returns the index of the target that the vehicle tries to move to right now.")) {
	OutVal(0, RInt, index, TFS("Index"), TFS("The index of the current target."))
	Body()
	UFGTargetPointLinkedList* List = self->GetTargetNodeLinkedList();
	index = (int64)TargetToIndex(List->GetCurrentTarget(), List);
} EndFunc()
BeginFunc(nextTarget, TFS("Next Target"), TFS("Sets the current target to the next target in the list.")) {
	Body()
	self->GetTargetNodeLinkedList()->SetNextTarget();
} EndFunc()
BeginFunc(setCurrentTarget, TFS("Set Current Target"), TFS("Sets the target with the given index as the target this vehicle tries to move to right now.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The index of the target this vehicle should move to now."))
	Body()
	UFGTargetPointLinkedList* List = self->GetTargetNodeLinkedList();
	AFGTargetPoint* Target = IndexToTarget(index, List);
	// TODO: Exception - if (!Target) throw FFINException("index out of range");
	List->SetCurrentTarget(Target);
} EndFunc()
BeginFunc(getTarget, TFS("Get Target"), TFS("Returns the target struct at with the given index in the target list.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The index of the target you want to get the struct from."))
	OutVal(0, RStruct<FFINTargetPoint>, target, TFS("Target"), TFS("The TargetPoint-Struct with the given index in the target list."))
	Body()
	UFGTargetPointLinkedList* List = self->GetTargetNodeLinkedList();
	AFGTargetPoint* Target = IndexToTarget(index, List);
	// TODO: Exception - if (!Target) throw FFINException("index out of range");
	target = (FINAny)FFINTargetPoint(Target);
} EndFunc()
BeginFunc(removeTarget, TFS("Remove Target"), TFS("Removes the target with the given index from the target list.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The index of the target point you want to remove from the target list."))
	Body()
	UFGTargetPointLinkedList* List = self->GetTargetNodeLinkedList();
	AFGTargetPoint* Target = IndexToTarget(index, List);
	// TODO: Exception - if (!Target) throw FFINException( "index out of range");
	List->RemoveItem(Target);
	Target->Destroy();
} EndFunc()
BeginFunc(addTarget, TFS("Add Target"), TFS("Adds the given target point struct at the end of the target list.")) {
	InVal(0, RStruct<FFINTargetPoint>, target, TFS("Target"), TFS("The target point you want to add."))
	Body()
	AFGTargetPoint* Target = target.ToWheeledTargetPoint(self);
	// TODO: Exception - if (!Target) throw FFINException("failed to create target");
	self->GetTargetNodeLinkedList()->InsertItem(Target);
} EndFunc()
BeginFunc(setTarget, TFS("Set Target"), TFS("Allows to set the target at the given index to the given target point struct.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The index of the target point you want to update with the given target point struct."))
	InVal(1, RStruct<FFINTargetPoint>, target, TFS("Target"), TFS("The new target point struct for the given index."))
	Body()
	UFGTargetPointLinkedList* List = self->GetTargetNodeLinkedList();
	AFGTargetPoint* Target = IndexToTarget(index, List);
	// TODO: Excpetion - if (!Target) throw FFINException("index out of range");
	Target->SetActorLocation(target.Pos);
	Target->SetActorRotation(target.Rot);
	Target->SetTargetSpeed(target.Speed);
	Target->SetWaitTime(target.Wait);
} EndFunc()
BeginFunc(clearTargets, TFS("Clear Targets"), TFS("Removes all targets from the target point list.")) {
	Body()
	self->GetTargetNodeLinkedList()->ClearRecording();
} EndFunc()
BeginFunc(getTargets, TFS("Get Targets"), TFS("Returns a list of target point structs of all the targets in the target point list.")) {
	OutVal(0, RArray<RStruct<FFINTargetPoint>>, targets, TFS("Targets"), TFS("A list of target point structs containing all the targets of the target point list."))
	Body()
	TArray<FINAny> Targets;
	UFGTargetPointLinkedList* List = self->GetTargetNodeLinkedList();
	AFGTargetPoint* CurrentTarget = nullptr;
	int i = 0;
	do {
		if (i++) CurrentTarget = CurrentTarget->mNext;
		else CurrentTarget = List->GetFirstTarget();
		Targets.Add((FINAny)FFINTargetPoint(CurrentTarget));
	} while (CurrentTarget && CurrentTarget != List->GetLastTarget());
	targets = Targets;
} EndFunc()
BeginFunc(setTargets, TFS("Set Targets"), TFS("Removes all targets from the target point list and adds the given array of target point structs to the empty target point list.")) {
	InVal(0, RArray<RStruct<FFINTargetPoint>>, targets, TFS("Targets"), TFS("A list of target point structs you want to place into the empty target point list."))
	Body()
	UFGTargetPointLinkedList* List = self->GetTargetNodeLinkedList();
	List->ClearRecording();
	for (const FINAny& Target : targets) {
		List->InsertItem(Target.GetStruct().Get<FFINTargetPoint>().ToWheeledTargetPoint(self));
	}
} EndFunc()
BeginProp(RFloat, speed, TFS("Speed"), TFS("The current forward speed of this vehicle.")) {
	Return self->GetForwardSpeed();
} EndProp()
BeginProp(RFloat, burnRatio, TFS("Burn Ratio"), TFS("The amount of fuel this vehicle burns.")) {
	Return self->GetFuelBurnRatio();
} EndProp()
BeginProp(RInt, wheelsOnGround, TFS("Wheels On Ground"), TFS("The number of wheels currenlty on the ground.")) {
	Return (int64)self->NumWheelsOnGround();
} EndProp()
BeginProp(RBool, hasFuel, TFS("Has Fuel"), TFS("True if the vehicle has currently fuel to drive.")) {
	Return self->HasFuel();
} EndProp()
BeginProp(RBool, isInAir, TFS("Is In Air"), TFS("True if the vehicle is currently in the air.")) {
	Return self->GetIsInAir();
} EndProp()
BeginProp(RBool, wantsToMove, TFS("Wants To Move"), TFS("True if the vehicle currently wants to move.")) {
	Return self->WantsToMove();
} EndProp()
BeginProp(RBool, isDrifting, TFS("Is Drifting"), TFS("True if the vehicle is currently drifting.")) {
	Return self->GetIsDrifting();
} EndProp()
EndType()

BeginType(AFGBuildableTrainPlatform, "TrainPlatform", TFS("Train Platform"), TFS("The base class for all train station parts."))
BeginFunc(getTrackGraph, TFS("Get Track Graph"), TFS("Returns the track graph of which this platform is part of.")) {
	OutVal(0, RStruct<FFINTrackGraph>, graph, TFS("Graph"), TFS("The track graph of which this platform is part of."))
	Body()
	graph = (FINAny)FFINTrackGraph{Ctx, self->GetTrackGraphID()};
} EndFunc()
BeginFunc(getTrackPos, TFS("Get Track Pos"), TFS("Returns the track pos at which this train platform is placed.")) {
	OutVal(0, RTrace, track, TFS("Track"), TFS("The track the track pos points to."))
	OutVal(1, RFloat, offset, TFS("Offset"), TFS("The offset of the track pos."))
	OutVal(2, RFloat, forward, TFS("Forward"), TFS("The forward direction of the track pos. 1 = with the track direction, -1 = against the track direction"))
	Body()
	FRailroadTrackPosition pos = self->GetTrackPosition();
	if (!pos.IsValid()) return; // TODO: Exception
	track = Ctx(pos.Track.Get());
	offset = pos.Offset;
	forward = pos.Forward;
} EndFunc()
BeginFunc(getConnectedPlatform, TFS("Get Connected Platform"), TFS("Returns the connected platform in the given direction.")) {
	InVal(0, RInt, direction, TFS("Direction"), TFS("The direction in which you want to get the connected platform."))
	OutVal(1, RTrace, platform, TFS("Platform"), TFS("The platform connected to this platform in the given direction."))
	Body()
	platform = Ctx / self->GetConnectedPlatformInDirectionOf(direction);
} EndFunc()
BeginFunc(getDockedVehicle, TFS("Get Docked Vehicle"), TFS("Returns the currently docked vehicle.")) {
	OutVal(0, RTrace, vehicle, TFS("Vehicle"), TFS("The currently docked vehicle"))
	Body()
	vehicle = Ctx / FReflectionHelper::GetObjectPropertyValue<UObject>(self, TEXT("mDockedRailroadVehicle"));
} EndFunc()
BeginFunc(getMaster, TFS("Get Master"), TFS("Returns the master platform of this train station.")) {
	OutVal(0, RTrace, master, TFS("Master"), TFS("The master platform of this train station."))
	Body()
	master = Ctx / FReflectionHelper::GetObjectPropertyValue<UObject>(self, TEXT("mStationDockingMaster"));
} EndFunc()
BeginFunc(getDockedLocomotive, TFS("Get Docked Locomotive"), TFS("Returns the currently docked locomotive at the train station.")) {
	OutVal(0, RTrace, locomotive, TFS("Locomotive"), TFS("The currently docked locomotive at the train station."))
	Body()
	locomotive = Ctx / FReflectionHelper::GetObjectPropertyValue<UObject>(self, TEXT("mDockingLocomotive"));
} EndFunc()
BeginProp(RInt, status, TFS("Status"), TFS("The current docking status of the platform.")) {
	Return (int64)self->GetDockingStatus();
} EndProp()
BeginProp(RBool, isReversed, TFS("Is Reversed"), TFS("True if the orientation of the platform is reversed relative to the track/station.")) {
	Return self->IsOrientationReversed();
} EndProp()
EndType()

BeginType(AFGBuildableRailroadStation, "RailroadStation", TFS("Railroad Station"), TFS("The train station master platform. This platform holds the name and manages docking of trains."))
BeginProp(RString, name, TFS("Name"), TFS("The name of the railroad station.")) {
	Return self->GetStationIdentifier()->GetStationName().ToString();
} PropSet() {
	self->GetStationIdentifier()->SetStationName(FText::FromString(Val));
} EndProp()
BeginProp(RInt, dockedOffset, TFS("Docked Offset"), TFS("The Offset to the beginning of the station at which trains dock.")) {
	Return self->GetDockedVehicleOffset();
} EndProp()
EndType()

BeginType(AFGBuildableTrainPlatformCargo, "TrainPlatformCargo", TFS("Train Platform Cargo"), TFS("A train platform that allows for loading and unloading cargo cars."))
BeginProp(RBool, isLoading, TFS("Is Loading"), TFS("True if the cargo platform is currently loading the docked cargo vehicle.")) {
	Return self->GetIsInLoadMode();
} EndProp()
BeginProp(RBool, isUnloading, TFS("Is Unloading"), TFS("True if the cargo platform is currently unloading the docked cargo vehicle.")) {
	Return self->IsLoadUnloading();
} EndProp()
BeginProp(RFloat, dockedOffset, TFS("Docked Offset"), TFS("The offset to the track start of the platform at were the vehicle docked.")) {
	Return self->GetDockedVehicleOffset();
} EndProp()
BeginProp(RFloat, outputFlow, TFS("Output Flow"), TFS("The current output flow rate.")) {
	Return self->GetOutflowRate();
} EndProp()
BeginProp(RFloat, inputFlow, TFS("Input Flow"), TFS("The current input flow rate.")) {
	Return self->GetInflowRate();
} EndProp()
BeginProp(RBool, fullLoad, TFS("Full Load"), TFS("True if the docked cargo vehicle is fully loaded.")) {
	Return (bool)self->IsFullLoad();
} EndProp()
BeginProp(RBool, fullUnload, TFS("Full Unload"), TFS("Ture if the docked cargo vehicle is fully unloaded.")) {
	Return (bool)self->IsFullUnload();
} EndProp()
EndType()

BeginType(AFGRailroadVehicle, "RailroadVehicle", TFS("Railroad Vehicle"), TFS("The base class for any vehicle that drives on train tracks."))
BeginFunc(getTrain, TFS("Get Train"), TFS("Returns the train of which this vehicle is part of.")) {
	OutVal(0, RTrace, train, TFS("Train"), TFS("The train of which this vehicle is part of"))
	Body()
	train = Ctx / Cast<UObject>(self->GetTrain());
} EndFunc()
BeginFunc(isCoupled, TFS("Is Coupled"), TFS("Allows to check if the given coupler is coupled to another car.")) {
	InVal(0, RInt, coupler, TFS("Coupler"), TFS("The Coupler you want to check. 0 = Front, 1 = Back"))
	OutVal(1, RBool, coupled, TFS("Coupled"), TFS("True of the give coupler is coupled to another car."))
	Body()
	coupled = self->IsCoupledAt(static_cast<ERailroadVehicleCoupler>(coupler));
} EndFunc()
BeginFunc(getCoupled, TFS("Get Coupled"), TFS("Allows to get the coupled vehicle at the given coupler.")) {
	InVal(0, RInt, coupler, TFS("Coupler"), TFS("The Coupler you want to get the car from. 0 = Front, 1 = Back"))
	OutVal(1, RTrace, coupled, TFS("Coupled"), TFS("The coupled car of the given coupler is coupled to another car."))
	Body()
	coupled = Ctx / self->GetCoupledVehicleAt(static_cast<ERailroadVehicleCoupler>(coupler));
} EndFunc()
BeginFunc(getTrackGraph, TFS("Get Track Graph"), TFS("Returns the track graph of which this vehicle is part of.")) {
	OutVal(0, RStruct<FFINTrackGraph>, track, TFS("Track"), TFS("The track graph of which this vehicle is part of."))
	Body()
	track = (FINAny)FFINTrackGraph{Ctx, self->GetTrackGraphID()};
} EndFunc()
BeginFunc(getTrackPos, TFS("Get Track Pos"), TFS("Returns the track pos at which this vehicle is.")) {
	OutVal(0, RTrace, track, TFS("Track"), TFS("The track the track pos points to."))
    OutVal(1, RFloat, offset, TFS("Offset"), TFS("The offset of the track pos."))
    OutVal(2, RFloat, forward, TFS("Forward"), TFS("The forward direction of the track pos. 1 = with the track direction, -1 = against the track direction"))
    Body()
    FRailroadTrackPosition pos = self->GetTrackPosition();
	if (!pos.IsValid()) return; // TODO: Exception
	track = Ctx(pos.Track.Get());
	offset = pos.Offset;
	forward = pos.Forward;
} EndFunc()
BeginFunc(getMovement, TFS("Get Movement"), TFS("Returns the vehicle movement of this vehicle.")) {
	OutVal(0, RTrace, movement, TFS("Movement"), TFS("The movement of this vehicle."))
	Body()
	movement = Ctx / self->GetRailroadVehicleMovementComponent();
} EndFunc()
BeginProp(RFloat, length, TFS("Length"), TFS("The length of this vehicle on the track.")) {
	Return self->GetLength();
} EndProp()
BeginProp(RBool, isDocked, TFS("Is Docked"), TFS("True if this vehicle is currently docked to a platform.")) {
	Return self->IsDocked();
} EndProp()
BeginProp(RBool, isReversed, TFS("Is Reversed"), TFS("True if the vheicle is placed reversed on the track.")) {
	Return self->IsOrientationReversed();
} EndProp()
EndType()

BeginType(UFGRailroadVehicleMovementComponent, "RailroadVehicleMovement", TFS("Railroad Vehicle Movement"), TFS("This actor component contains all the infomation about the movement of a railroad vehicle."))
BeginFunc(getVehicle, TFS("Get Vehicle"), TFS("Returns the vehicle this movement component holds the movement information of.")) {
	OutVal(0, RTrace, vehicle, TFS("Vehicle"), TFS("The vehicle this movement component holds the movement information of."))
	Body()
	vehicle = Ctx / self->GetOwningRailroadVehicle();
} EndFunc()
BeginFunc(getWheelsetRotation, TFS("Get Wheelset Rotation"), TFS("Returns the current rotation of the given wheelset.")){
	InVal(0, RInt, wheelset, TFS("Wheelset"), TFS("The index of the wheelset you want to get the rotation of."))
	OutVal(1, RFloat, x, TFS("X"), TFS("The wheelset's rotation X component."))
	OutVal(2, RFloat, y, TFS("Y"), TFS("The wheelset's rotation Y component."))
	OutVal(3, RFloat, z, TFS("Z"), TFS("The wheelset's rotation Z component."))
	Body()
	FVector rot = self->GetWheelsetRotation(wheelset);
	x = rot.X;
	y = rot.Y;
	z = rot.Z;
} EndFunc()
BeginFunc(getWheelsetOffset, TFS("Get Wheelset Offset"), TFS("Returns the offset of the wheelset with the given index from the start of the vehicle.")) {
	InVal(0, RInt, wheelset, TFS("Wheelset"), TFS("The index of the wheelset you want to get the offset of."))
	OutVal(1, RFloat, offset, TFS("Offset"), TFS("The offset of the wheelset."))
	Body()
	offset = self->GetWheelsetOffset(wheelset);
} EndFunc()
BeginFunc(getCouplerRotationAndExtention, TFS("Get Coupler Rotation And Extention"), TFS("Returns the normal vector and the extention of the coupler with the given index.")) {
	InVal(0, RInt, coupler, TFS("Coupler"), TFS("The index of which you want to get the normal and extention of."))
	OutVal(1, RFloat, x, TFS("X"), TFS("The X component of the coupler normal."))
	OutVal(2, RFloat, y, TFS("Y"), TFS("The Y component of the coupler normal."))
	OutVal(3, RFloat, z, TFS("Z"), TFS("The Z component of the coupler normal."))
	OutVal(4, RFloat, extention, TFS("Extention"), TFS("The extention of the coupler."))
	Body()
	float extension;
	FVector rotation = self->GetCouplerRotationAndExtention(coupler, extension);
	x =rotation.X;
	y = rotation.Y;
	z = rotation.Z;
	extention = extension;
} EndFunc()

BeginProp(RFloat, orientation, TFS("Orientation"), TFS("The orientation of the vehicle")) {
	Return self->GetOrientation();
} EndProp()
BeginProp(RFloat, mass, TFS("Mass"), TFS("The current mass of the vehicle.")) {
	Return self->GetMass();
} EndProp()
BeginProp(RFloat, tareMass, TFS("Tare Mass"), TFS("The tare mass of the vehicle.")) {
	Return self->GetTareMass();
} EndProp()
BeginProp(RFloat, payloadMass, TFS("Payload Mass"), TFS("The mass of the payload of the vehicle.")) {
	Return self->GetPayloadMass();
} EndProp()
BeginProp(RFloat, speed, TFS("Speed"), TFS("The current forward speed of the vehicle.")) {
	Return self->GetForwardSpeed();
} EndProp()
BeginProp(RFloat, relativeSpeed, TFS("Relative Speed"), TFS("The current relative forward speed to the ground.")) {
	Return self->GetRelativeForwardSpeed();
} EndProp()
BeginProp(RFloat, maxSpeed, TFS("Max Speed"), TFS("The maximum forward speed the vehicle can reach.")) {
	Return self->GetMaxForwardSpeed();
} EndProp()
BeginProp(RFloat, gravitationalForce, TFS("Gravitationl Force"), TFS("The current gravitational force acting on the vehicle.")) {
	Return self->GetGravitationalForce();
} EndProp()
BeginProp(RFloat, tractiveForce, TFS("Tractive Force"), TFS("The current tractive force acting on the vehicle.")) {
	Return self->GetTractiveForce();
} EndProp()
BeginProp(RFloat, resistiveForce, TFS("Resistive Froce"), TFS("The resistive force currently acting on the vehicle.")) {
	Return self->GetResistiveForce();
} EndProp()
BeginProp(RFloat, gradientForce, TFS("Gradient Force"), TFS("The gradient force currently acting on the vehicle.")) {
	Return self->GetGradientForce();
} EndProp()
BeginProp(RFloat, brakingForce, TFS("Braking Force"), TFS("The braking force currently acting on the vehicle.")) {
	Return self->GetBrakingForce();
} EndProp()
BeginProp(RFloat, airBrakingForce, TFS("Air Braking Force"), TFS("The air braking force currently acting on the vehicle.")) {
	Return self->GetAirBrakingForce();
} EndProp()
BeginProp(RFloat, dynamicBrakingForce, TFS("Dynamic Braking Force"), TFS("The dynamic braking force currently acting on the vehicle.")) {
	Return self->GetDynamicBrakingForce();
} EndProp()
BeginProp(RFloat, maxTractiveEffort, TFS("Max Tractive Effort"), TFS("The maximum tractive effort of this vehicle.")) {
	Return self->GetMaxTractiveEffort();
} EndProp()
BeginProp(RFloat, maxDynamicBrakingEffort, TFS("Max Dynamic Braking Effort"), TFS("The maximum dynamic braking effort of this vehicle.")) {
	Return self->GetMaxDynamicBrakingEffort();
} EndProp()
BeginProp(RFloat, maxAirBrakingEffort, TFS("Max Air Braking Effort"), TFS("The maximum air braking effort of this vehcile.")) {
	Return self->GetMaxAirBrakingEffort();
} EndProp()
BeginProp(RFloat, trackGrade, TFS("Track Grade"), TFS("The current track grade of this vehicle.")) {
	Return self->GetTrackGrade();
} EndProp()
BeginProp(RFloat, trackCurvature, TFS("Track Curvature"), TFS("The current track curvature of this vehicle.")) {
	Return self->GetTrackCurvature();
} EndProp()
BeginProp(RFloat, wheelsetAngle, TFS("Wheelset Angle"), TFS("The wheelset angle of this vehicle.")) {
	Return self->GetWheelsetAngle();
} EndProp()
BeginProp(RFloat, rollingResistance, TFS("Rolling Resistance"), TFS("The current rolling resistance of this vehicle.")) {
	Return self->GetRollingResistance();
} EndProp()
BeginProp(RFloat, curvatureResistance, TFS("Curvature Resistance"), TFS("The current curvature resistance of this vehicle.")) {
	Return self->GetCurvatureResistance();
} EndProp()
BeginProp(RFloat, airResistance, TFS("Air Resistance"), TFS("The current air resistance of this vehicle.")) {
	Return self->GetAirResistance();
} EndProp()
BeginProp(RFloat, gradientResistance, TFS("Gradient Resistance"), TFS("The current gardient resistance of this vehicle.")) {
	Return self->GetGradientResistance();
} EndProp()
BeginProp(RFloat, wheelRotation, TFS("Wheel Rotation"), TFS("The current wheel rotation of this vehicle.")) {
	Return self->GetWheelRotation();
} EndProp()
BeginProp(RInt, numWheelsets, TFS("Num Wheelsets"), TFS("The number of wheelsets this vehicle has.")) {
	Return (int64)self->GetNumWheelsets();
} EndProp()
BeginProp(RBool, isMoving, TFS("Is Moving"), TFS("True if this vehicle is currently moving.")) {
	Return self->IsMoving();
} EndProp()
EndType()

BeginType(AFGTrain, "Train", TFS("Train"), TFS("This class holds information and references about a trains (a collection of multiple railroad vehicles) and its timetable f.e."))
// TODO: Signals & Hooks - LuaLibHook(AFGTrain, UFINTrainHook);
BeginFunc(getName, TFS("Get Name"), TFS("Returns the name of this train.")) {
	OutVal(0, RString, name, TFS("Name"), TFS("The name of this train."))
	Body()
	name = self->GetTrainName().ToString();
} EndFunc()
BeginFunc(setName, TFS("Set Name"), TFS("Allows to set the name of this train.")) {
	InVal(0, RString, name, TFS("Name"), TFS("The new name of this trian."))
	Body()
	self->SetTrainName(FText::FromString(name));
} EndFunc()
BeginFunc(getTrackGraph, TFS("Get Track Graph"), TFS("Returns the track graph of which this train is part of.")) {
	OutVal(0, RStruct<FFINTrackGraph>, track, TFS("Track"), TFS("The track graph of which this train is part of."))
	Body()
	track = (FINAny) FFINTrackGraph{Ctx, self->GetTrackGraphID()};
} EndFunc()
BeginFunc(setSelfDriving, TFS("Set Self Driving"), TFS("Allows to set if the train should be self driving or not.")) {
	InVal(0, RBool, selfDriving, TFS("Self Driving"), TFS("True if the train should be self driving."))
	Body()
	self->SetSelfDrivingEnabled(selfDriving);
} EndFunc()
BeginFunc(getMaster, TFS("Get Master"), TFS("Returns the master locomotive that is part of this train.")) {
	OutVal(0, RTrace, master, TFS("Master"), TFS("The master locomotive of this train."))
	Body()
	//master = Ctx / self->GetMultipleUnitMaster();
} EndFunc()
BeginFunc(getTimeTable, TFS("Get Time Table"), TFS("Returns the timetable of this train.")) {
	OutVal(0, RTrace, timeTable, TFS("Time Table"), TFS("The timetable of this train."))
	Body()
	timeTable = Ctx / self->GetTimeTable();
} EndFunc()
BeginFunc(newTimeTable, TFS("New Time Table"), TFS("Creates and returns a new timetable for this train.")) {
	OutVal(0, RTrace, timeTable, TFS("Time Table"), TFS("The new timetable for this train."))
	Body()
	timeTable = Ctx / self->NewTimeTable();
} EndFunc()
BeginFunc(getFirst, TFS("Get First"), TFS("Returns the first railroad vehicle that is part of this train.")) {
	OutVal(0, RTrace, first, TFS("First"), TFS("The first railroad vehicle that is part of this train."))
	Body()
	first = Ctx / self->GetFirstVehicle();
} EndFunc()
BeginFunc(getLast, TFS("Get Last"), TFS("Returns the last railroad vehicle that is part of this train.")) {
	OutVal(0, RTrace, last, TFS("Last"), TFS("The last railroad vehicle that is part of this train."))
	Body()
	last = Ctx / self->GetLastVehicle();
} EndFunc()
BeginFunc(dock, TFS("Dock"), TFS("Trys to dock the train to the station it is currently at.")) {
	Body()
	self->Dock();
} EndFunc()
BeginFunc(getVehicles, TFS("Get Vehicles"), TFS("Returns a list of all the vehicles this train has.")) {
	OutVal(0, RArray<RTrace>, vehicles, TFS("Vehicles"), TFS("A list of all the vehicles this train has."))
	Body()
	TArray<FINAny> Vehicles;
	for (AFGRailroadVehicle* vehicle : self->mSimulationData.SimulatedVehicles) {
		Vehicles.Add(Ctx / vehicle);
	}
	vehicles = Vehicles;
} EndFunc()
BeginProp(RBool, isPlayerDriven, TFS("Is Player Driven"), TFS("True if the train is currently player driven.")) {
	Return self->IsPlayerDriven();
} EndProp()
BeginProp(RBool, isSelfDriving, TFS("Is Self Driving"), TFS("True if the train is currently self driving.")) {
	Return self->IsSelfDrivingEnabled();
} EndProp()
BeginProp(RInt, selfDrivingError, TFS("Self Driving Error"), TFS("The last self driving error.\n0 = No Error\n1 = No Power\n2 = No Time Table\n3 = Invalid Next Stop\n4 = Invalid Locomotive Placement\n5 = No Path")) {
	Return (int64)self->GetSelfDrivingError();
} EndProp()
BeginProp(RBool, hasTimeTable, TFS("Has Time Table"), TFS("True if the train has currently a time table.")) {
	Return self->HasTimeTable();
} EndProp()
BeginProp(RInt, dockState, TFS("Dock State"), TFS("The current docking state of the train.")) {
	Return (int64)self->GetDockingState();
} EndProp()
BeginProp(RBool, isDocked, TFS("Is Docked"), TFS("True if the train is currently docked.")) {
	Return self->IsDocked();
} EndProp()
EndType()

BeginType(AFGRailroadTimeTable, "TimeTable", TFS("Time Table"), TFS("Contains the time table information of train."))
BeginFunc(addStop, TFS("Add Stop"), TFS("Adds a stop to the time table.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The index at which the stop should get added."))
	InVal(1, RTrace, station, TFS("Station"), TFS("The railroad station at which the stop should happen."))
	InVal(2, RFloat, duration, TFS("Duration"), TFS("The duration how long the train should stop at the station."))
	OutVal(3, RBool, added, TFS("Added"), TFS("True if the stop got sucessfully added to the time table."))
	Body()
	FTimeTableStop stop;
	stop.Station = Cast<AFGBuildableRailroadStation>(station.Get())->GetStationIdentifier();
	stop.Duration =duration;
	added = self->AddStop(index, stop);
} EndFunc()
BeginFunc(removeStop, TFS("Remove Stop"), TFS("Removes the stop with the given index from the time table.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The index at which the stop should get added."))
	Body()
	self->RemoveStop(index);
} EndFunc()
BeginFunc(getStops, TFS("Get Stops"), TFS("Returns a list of all the stops this time table has")) {
	OutVal(0, RArray<RStruct<FFINTimeTableStop>>, stops, TFS("Stops"), TFS("A list of time table stops this time table has."))
	Body()
	TArray<FINAny> Output;
	TArray<FTimeTableStop> Stops;
	self->GetStops(Stops);
	for (const FTimeTableStop& Stop : Stops) {
		Output.Add((FINAny)FFINTimeTableStop{Ctx / Stop.Station->GetStation(), Stop.Duration});
	}
	stops = Output;
} EndFunc()
BeginFunc(setStops, TFS("Set Stops"), TFS("Allows to empty and fill the stops of this time table with the given list of new stops.")) {
	InVal(0, RArray<RStruct<FFINTimeTableStop>>, stops, TFS("Stops"), TFS("The new time table stops."))
	OutVal(0, RBool, gotSet, TFS("Got Set"), TFS("True if the stops got sucessfully set."))
	Body()
	TArray<FTimeTableStop> Stops;
	for (const FINAny& Any : stops) {
		Stops.Add(Any.GetStruct().Get<FFINTimeTableStop>());
	}
	gotSet = self->SetStops(Stops);
} EndFunc()
BeginFunc(isValidStop, TFS("Is Valid Stop"), TFS("Allows to check if the given stop index is valid.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The stop index you want to check its validity."))
	OutVal(1, RBool, valid, TFS("Valid"), TFS("True if the stop index is valid."))
	Body()
	valid = self->IsValidStop(index);
} EndFunc()
BeginFunc(getStop, TFS("Get Stop"), TFS("Returns the stop at the given index.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The index of the stop you want to get."))
	OutVal(1, RStruct<FFINTimeTableStop>, stop, TFS("Stop"), TFS("The time table stop at the given index."))
	Body()
	FTimeTableStop Stop = self->GetStop(index);
	if (IsValid(Stop.Station)) {
		stop = (FINAny)FFINTimeTableStop{Ctx / Stop.Station->GetStation(), Stop.Duration};
	} else {
		stop = FINAny();
	}
} EndFunc()
BeginFunc(setCurrentStop, TFS("Set Current Stop"), TFS("Sets the stop, to which the train trys to drive to right now.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The index of the stop the train should drive to right now."))
	Body()
	self->SetCurrentStop(index);
} EndFunc()
BeginFunc(incrementCurrentStop, TFS("Increment Current Stop"), TFS("Sets the current stop to the next stop in the time table.")) {
	Body()
	self->IncrementCurrentStop();
} EndFunc()
BeginFunc(getCurrentStop, TFS("Get Current Stop"), TFS("Returns the index of the stop the train drives to right now.")) {
	OutVal(0, RInt, index, TFS("Index"), TFS("The index of the stop the train tries to drive to right now."))
    Body()
    index = (int64) self->GetCurrentStop();
} EndFunc()
BeginProp(RInt, numStops, TFS("Num Stops"), TFS("The current number of stops in the time table.")) {
	Return (int64)self->GetNumStops();
} EndProp()
EndType()

BeginType(AFGBuildableRailroadTrack, "RailroadTrack", TFS("Railroad Track"), TFS("A peice of railroad track over which trains can drive."))
BeginFunc(getClosestTrackPosition, TFS("Get Closeset Track Position"), TFS("Returns the closes track position from the given world position")) {
	InVal(0, RStruct<FVector>, worldPos, TFS("World Pos"), TFS("The world position form which you want to get the closest track position."))
	OutVal(1, RTrace, track, TFS("Track"), TFS("The track the track pos points to."))
    OutVal(2, RFloat, offset, TFS("Offset"), TFS("The offset of the track pos."))
    OutVal(3, RFloat, forward, TFS("Forward"), TFS("The forward direction of the track pos. 1 = with the track direction, -1 = against the track direction"))
    Body()
	FRailroadTrackPosition pos = self->FindTrackPositionClosestToWorldLocation(worldPos);
	if (!pos.IsValid()) return; // TODO: Exception
	track = Ctx(pos.Track.Get());
	offset = pos.Offset;
	forward = pos.Forward;
} EndFunc()
BeginFunc(getWorldLocAndRotAtPos, TFS("Get World Location And Rotation At Position"), TFS("Returns the world location and world rotation of the track position from the given track position.")) {
	InVal(0, RTrace, track, TFS("Track"), TFS("The track the track pos points to."))
    InVal(1, RFloat, offset, TFS("Offset"), TFS("The offset of the track pos."))
    InVal(2, RFloat, forward, TFS("Forward"), TFS("The forward direction of the track pos. 1 = with the track direction, -1 = against the track direction"))
    OutVal(3, RStruct<FVector>, location, TFS("Location"), TFS("The location at the given track position"))
	OutVal(4, RStruct<FVector>, rotation, TFS("Rotation"), TFS("The rotation at the given track position (forward vector)"))
	Body()
	FRailroadTrackPosition pos(Cast<AFGBuildableRailroadTrack>(track.Get()), offset, forward);
	FVector loc;
	FVector rot;
	self->GetWorldLocationAndDirectionAtPosition(pos, loc, rot);
	location = (FINAny)loc;
	rotation = (FINAny)rot;
} EndFunc()
BeginFunc(getConnection, TFS("Get Connection"), TFS("Returns the railroad track connection at the given direction.")) {
	InVal(0, RInt, direction, TFS("Direction"), TFS("The direction of which you want to get the connector from. 0 = front, 1 = back"))
	OutVal(1, RTrace, connection, TFS("Connection"), TFS("The connection component in the given direction."))
	Body()
	connection = Ctx / self->GetConnection(direction);
} EndFunc()
BeginFunc(getTrackGraph, TFS("Get Track Graph"), TFS("Returns the track graph of which this track is part of.")) {
	OutVal(0, RStruct<FFINTrackGraph>, track, TFS("Track"), TFS("The track graph of which this track is part of."))
    Body()
    track = (FINAny)FFINTrackGraph{Ctx, self->GetTrackGraphID()};
} EndFunc()
BeginProp(RFloat, length, TFS("Length"), TFS("The length of the track.")) {
	Return self->GetLength();
} EndProp()
BeginProp(RBool, isOwnedByPlatform, TFS("Is Owned By Platform"), TFS("True if the track is part of/owned by a railroad platform.")) {
	Return self->GetIsOwnedByPlatform();
} EndProp()
EndType()

BeginType(UFGRailroadTrackConnectionComponent, "RailroadTrackConnection", TFS("Railroad Track Connection"), TFS("This is a actor component for railroad tracks that allows to connecto to other track connections and so to connection multiple tracks with each eather so you can build a train network."))
BeginProp(RStruct<FVector>, connectorLocation, TFS("Connector Location"), TFS("The world location of the the connection.")) {
	Return self->GetConnectorLocation();
} EndProp()
BeginProp(RStruct<FVector>, connectorNormal, TFS("Connector Normal"), TFS("The normal vecotr of the connector.")) {
	Return self->GetConnectorNormal();
} EndProp()
BeginFunc(getConnection, TFS("Get Connection"), TFS("Returns the connected connection with the given index.")) {
	InVal(1, RInt, index, TFS("Index"), TFS("The index of the connected connection you want to get."))
	OutVal(0, RTrace, connection, TFS("Connection"), TFS("The connected connection at the given index."))
	Body()
	connection = Ctx / self->GetConnection(index);
} EndFunc()
BeginFunc(getConnections, TFS("Get Connections"), TFS("Returns a list of all connected connections.")) {
	OutVal(0, RArray<RTrace>, connections, TFS("Connections"), TFS("A list of all connected connections."))
	Body()
	TArray<FINAny> Connections;
	for (UFGRailroadTrackConnectionComponent* conn : self->GetConnections()) {
		Connections.Add(Ctx / conn);
	}
	connections = Connections;
} EndFunc()
BeginFunc(getTrackPos, TFS("Get Track Pos"), TFS("Returns the track pos at which this connection is.")) {
	OutVal(0, RTrace, track, TFS("Track"), TFS("The track the track pos points to."))
    OutVal(1, RFloat, offset, TFS("Offset"), TFS("The offset of the track pos."))
    OutVal(2, RFloat, forward, TFS("Forward"), TFS("The forward direction of the track pos. 1 = with the track direction, -1 = against the track direction"))
    Body()
    FRailroadTrackPosition pos = self->GetTrackPosition();
	if (!pos.IsValid()) return; // TODO: Exception
	track = Ctx(pos.Track.Get());
	offset = pos.Offset;
	forward = pos.Forward;
} EndFunc()
BeginFunc(getTrack, TFS("Get Track"), TFS("Returns the track of which this connection is part of.")) {
	OutVal(0, RTrace, track, TFS("Track"), TFS("The track of which this connection is part of."))
	Body()
	track = Ctx / self->GetTrack();
} EndFunc()
BeginFunc(getSwitchControl, TFS("Get Switch Control"), TFS("Returns the switch control of this connection.")) {
	OutVal(0, RTrace, switchControl, TFS("Switch"), TFS("The switch control of this connection."))
	Body()
	switchControl = Ctx / self->GetSwitchControl();
} EndFunc()
BeginFunc(getStation, TFS("Get Station"), TFS("Returns the station of which this connection is part of.")) {
	OutVal(0, RTrace, station, TFS("Station"), TFS("The station of which this connection is part of."))
	Body()
	station = Ctx / self->GetStation();
} EndFunc()
BeginFunc(getSignal, TFS("Get Signal"), TFS("Returns the signal of which this connection is part of.")) {
	OutVal(0, RTrace, signal, TFS("Signal"), TFS("The signal of which this connection is part of."))
	Body()
	signal = Ctx / self->GetSignal();
} EndFunc()
BeginFunc(getOpposite, TFS("Get Opposite"), TFS("Returns the opposite connection of the track this connection is part of.")) {
	OutVal(0, RTrace, opposite, TFS("Opposite"), TFS("The opposite connection of the track this connection is part of."))
	Body()
	opposite = Ctx / self->GetOpposite();
} EndFunc()
BeginFunc(getNext, TFS("Get Next"), TFS("Returns the next connection in the direction of the track. (used the correct path switched point to)")) {
	OutVal(0, RTrace, next, TFS("Next"), TFS("The next connection in the direction of the track."))
	Body()
	next = Ctx / self->GetNext();
} EndFunc()
BeginFunc(setSwitchPosition, TFS("Set Switch Position"), TFS("Sets the position (connection index) to which the track switch points to.")) {
	InVal(0, RInt, index, TFS("Index"), TFS("The connection index to which the switch should point to."))
	Body()
	self->SetSwitchPosition(index);
} EndFunc()
BeginFunc(getSwitchPosition, TFS("Get Switch Position"), TFS("Returns the current switch position.")) {
	OutVal(0, RInt, index, TFS("Index"), TFS("The index of the connection connection the switch currently points to."))
    Body()
    index = (int64)self->GetSwitchPosition();
} EndFunc()
BeginProp(RBool, isConnected, TFS("Is Connected"), TFS("True if the connection has any connection to other connections.")) {
	Return self->IsConnected();
} EndProp()
BeginProp(RBool, isFacingSwitch, TFS("Is Facing Switch"), TFS("True if this connection is pointing to the merge/spread point of the switch.")) {
	Return self->IsFacingSwitch();
} EndProp()
BeginProp(RBool, isTrailingSwitch, TFS("Is Trailing Switch"), TFS("True if this connection is pointing away from the merge/spread point of a switch.")) {
	Return self->IsTrailingSwitch();
} EndProp()
BeginProp(RInt, numSwitchPositions, TFS("Num Switch Positions"), TFS("Returns the number of different switch poisitions this switch can have.")) {
	Return (int64)self->GetNumSwitchPositions();
} EndProp()
EndType()

BeginType(AFGBuildableRailroadSwitchControl, "RailroadSwitchControl", TFS("Railroad Switch Control"), TFS("The controler object for a railroad switch."))
BeginFunc(toggleSwitch, TFS("Toggle Switch"), TFS("Toggles the railroad switch like if you interact with it.")) {
	Body()
	self->ToggleSwitchPosition();
} EndFunc()
BeginFunc(switchPosition, TFS("Switch Position"), TFS("Returns the current switch position of this switch.")) {
	OutVal(0, RInt, position, TFS("Position"), TFS("The current switch position of this switch."))
    Body()
    position = (int64)self->GetSwitchPosition();
} EndFunc()
EndType()

BeginType(AFGBuildableDockingStation, "DockingStation", TFS("Docking Station"), TFS("A docking station for wheeled vehicles to transfer cargo."))
BeginFunc(getFuelInv, TFS("Get Fueld Inventory"), TFS("Returns the fuel inventory of the docking station.")) {
	OutVal(0, RTrace, inventory, TFS("Inventory"), TFS("The fuel inventory of the docking station."))
	Body()
	inventory = Ctx / self->GetFuelInventory();
} EndFunc()
BeginFunc(getInv, TFS("Get Inventory"), TFS("Returns the cargo inventory of the docking staiton.")) {
	OutVal(0, RTrace, inventory, TFS("Inventory"), TFS("The cargo inventory of this docking station."))
	Body()
	inventory = Ctx / self->GetInventory();
} EndFunc()
BeginFunc(getDocked, TFS("Get Docked"), TFS("Returns the currently docked vehicle.")) {
	OutVal(0, RTrace, docked, TFS("Docked"), TFS("The currently docked vehicle."))
	Body()
	docked = Ctx / self->GetDockedActor();
} EndFunc()
BeginFunc(undock, TFS("Undock"), TFS("Undocked the currently docked vehicle from this docking station.")) {
	Body()
	self->Undock();
} EndFunc()
BeginProp(RBool, isLoadMode, TFS("Is Load Mode"), TFS("True if the docking station loads docked vehicles, flase if it unloads them.")) {
	Return self->GetIsInLoadMode();
} PropSet() {
	self->SetIsInLoadMode(Val);
} EndProp()
BeginProp(RBool, isLoadUnloading, TFS("Is Load Unloading"), TFS("True if the docking station is currently loading or unloading a docked vehicle.")) {
	Return self->IsLoadUnloading();
} EndProp()
EndType()

BeginType(AFGBuildablePipeReservoir, "PipeReservoir", TFS("Pipe Reservoir"), TFS("The base class for all fluid tanks."))
BeginFunc(flush, TFS("Flush"), TFS("Emptys the whole fluid container.")) {
	Body()
	AFGPipeSubsystem::Get(self->GetWorld())->FlushIntegrant(self);
} EndFunc()
BeginFunc(getFluidType, TFS("Get Fluid Type"), TFS("Returns the type of the fluid.")) {
	OutVal(0, RClass, type, TFS("Type"), TFS("The type of the fluid the tank contains."))
	Body()
	type = (UClass*)self->GetFluidDescriptor();
} EndFunc()
BeginProp(RFloat, fluidContent, TFS("Fluid Content"), TFS("The amount of fluid in the tank.")) {
	Return self->GetFluidBox()->Content;
} EndProp()
BeginProp(RFloat, maxFluidContent, TFS("Max Fluid Content"), TFS("The maximum amount of fluid this tank can hold.")) {
	Return self->GetFluidBox()->MaxContent;
} EndProp()
BeginProp(RFloat, flowFill, TFS("Flow Fill"), TFS("The currentl inflow rate of fluid.")) {
	Return self->GetFluidBox()->FlowFill;
} EndProp()
BeginProp(RFloat, flowDrain, TFS("Float Drain"), TFS("The current outflow rate of fluid.")) {
	Return self->GetFluidBox()->FlowDrain;
} EndProp()
BeginProp(RFloat, flowLimit, TFS("Flow Limit"), TFS("The maximum flow rate of fluid this tank can handle.")) {
	Return self->GetFluidBox()->FlowLimit;
} EndProp()
EndType()

BeginType(UFGRecipe, "Recipe", TFS("Recipe"), TFS("A struct that holds information about a recipe in its class. Means don't use it as object, use it as class type!"))
BeginClassProp(RString, name, TFS("Name"), TFS("The name of this recipe.")) {
	Return UFGRecipe::GetRecipeName(self).ToString();
} EndProp()
BeginClassProp(RFloat, duration, TFS("Duration"), TFS("The duration how much time it takes to cycle the recipe once.")) {
	Return UFGRecipe::GetManufacturingDuration(self);
} EndProp()
BeginClassFunc(getProducts, TFS("Get Products"), TFS("Returns a array of item amounts, this recipe returns (outputs) when the recipe is processed once."), false) {
	OutVal(0, RArray<RStruct<FItemAmount>>, products, TFS("Products"), TFS("The products of this recipe."))
	Body()
	TArray<FINAny> Products;
	for (const FItemAmount& Product : UFGRecipe::GetProducts(self)) {
		Products.Add((FINAny)Product);
	}
	products = Products;
} EndFunc()
BeginClassFunc(getIngredients, TFS("Get Ingredients"), TFS("Returns a array of item amounts, this recipe needs (input) so the recipe can be processed."), false) {
	OutVal(0, RArray<RStruct<FItemAmount>>, ingredients, TFS("Ingredients"), TFS("The ingredients of this recipe."))
	Body()
	TArray<FINAny> Ingredients;
	for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(self)) {
		Ingredients.Add((FINAny)Ingredient);
	}
	ingredients = Ingredients;
} EndFunc()
EndType()

BeginType(UFGItemDescriptor, "ItemType", TFS("Item Type"), TFS("The type of an item (iron plate, iron rod, leaves)"))
BeginClassProp(RString, name, TFS("Name"), TFS("The name of the item.")) {
	Return UFGItemDescriptor::GetItemName(self);
} EndProp()
EndType()