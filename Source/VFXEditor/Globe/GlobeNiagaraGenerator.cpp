// Copyright (c) 2026 Muhammad Jamal. Licensed under the MIT License.

#include "Globe/GlobeNiagaraGenerator.h"

#include "EdGraphSchema_Niagara.h"
#include "Globe/GlobeAssetUtils.h"
#include "Globe/GlobeVFXParameters.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraCommon.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeAssignment.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemFactoryNew.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

DEFINE_LOG_CATEGORY_STATIC(LogGlobeNiagara, Log, All);

namespace
{
	const TCHAR* EmitterName = TEXT("Surface");

	FNiagaraVariable UserVar(const FNiagaraTypeDefinition& Type, const TCHAR* Name)
	{
		return FNiagaraVariable(Type, FName(FString(TEXT("User.")) + Name));
	}

	/** Builder for one emitter graph: adds stock modules, sets static switches, links inputs, sets literals. */
	struct FStackBuilder
	{
		UNiagaraSystem* System = nullptr;
		FVersionedNiagaraEmitterData* Data = nullptr;
		UNiagaraGraph* Graph = nullptr;
		FVersionedNiagaraEmitter Emitter;
		FString UniqueEmitterName;
		TSet<FNiagaraVariableBase> KnownUserParams;
		bool bOk = true;

		UNiagaraNodeFunctionCall* FindModule(const TCHAR* FunctionName) const
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UNiagaraNodeFunctionCall* Call = Cast<UNiagaraNodeFunctionCall>(Node);
				if (Call && Call->FunctionScript && Call->FunctionScript->GetName() == FunctionName)
				{
					return Call;
				}
			}
			return nullptr;
		}

		UNiagaraNodeFunctionCall* AddModule(const TCHAR* ScriptPath, ENiagaraScriptUsage Usage)
		{
			UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, ScriptPath);
			UNiagaraNodeOutput* Output = Graph->FindEquivalentOutputNode(Usage);
			if (!Script || !Output)
			{
				UE_LOG(LogGlobeNiagara, Error, TEXT("Cannot add module %s (script %s, output %s)"), ScriptPath, Script ? TEXT("ok") : TEXT("missing"), Output ? TEXT("ok") : TEXT("missing"));
				bOk = false;
				return nullptr;
			}
			return FNiagaraStackGraphUtilities::AddScriptModuleToStack(Script, *Output);
		}

		static const UNiagaraScriptVariable* FindModuleInput(const UNiagaraNodeFunctionCall* Call, const TCHAR* InputName)
		{
			const UNiagaraGraph* Called = Call ? Call->GetCalledGraph() : nullptr;
			if (!Called)
			{
				return nullptr;
			}
			const FName FullName(FString(TEXT("Module.")) + InputName);
			for (const auto& Pair : Called->GetAllMetaData())
			{
				if (Pair.Key.GetName() == FullName)
				{
					return Pair.Value;
				}
			}
			return nullptr;
		}

		/** Static switch by enum display name ("Sphere", "Direct Set", ...) or bool ("true"/"false"). */
		void SetStatic(UNiagaraNodeFunctionCall* Call, const TCHAR* SwitchName, const TCHAR* Value)
		{
			if (!Call)
			{
				bOk = false;
				return;
			}
			const UNiagaraGraph* Called = Call->GetCalledGraph();
			FNiagaraVariable SwitchVar;
			for (const FNiagaraVariable& Var : Called->FindStaticSwitchInputs())
			{
				if (Var.GetName() == FName(SwitchName))
				{
					SwitchVar = Var;
				}
			}
			UEdGraphPin* Pin = nullptr;
			for (UEdGraphPin* Candidate : Call->Pins)
			{
				if (Candidate->Direction == EGPD_Input && Candidate->PinName == FName(SwitchName))
				{
					Pin = Candidate;
				}
			}
			if (!SwitchVar.IsValid() || !Pin)
			{
				UE_LOG(LogGlobeNiagara, Error, TEXT("%s: static switch '%s' not found"), *Call->GetFunctionName(), SwitchName);
				bOk = false;
				return;
			}

			FNiagaraVariable Literal(SwitchVar.GetType(), SwitchVar.GetName());
			Literal.AllocateData();
			if (const UEnum* Enum = SwitchVar.GetType().GetEnum())
			{
				int64 EnumValue = INDEX_NONE;
				for (int32 i = 0; i < Enum->NumEnums(); ++i)
				{
					if (Enum->GetDisplayNameTextByIndex(i).ToString() == Value)
					{
						EnumValue = Enum->GetValueByIndex(i);
					}
				}
				if (EnumValue == INDEX_NONE)
				{
					UE_LOG(LogGlobeNiagara, Error, TEXT("%s: '%s' is not a value of %s"), *Call->GetFunctionName(), Value, *Enum->GetName());
					bOk = false;
					return;
				}
				Literal.SetValue<int32>((int32)EnumValue);
			}
			else
			{
				FNiagaraBool Bool;
				Bool.SetValue(FCString::ToBool(Value));
				Literal.SetValue(Bool);
			}

			FString PinValue;
			if (!GetDefault<UEdGraphSchema_Niagara>()->TryGetPinDefaultValueFromNiagaraVariable(Literal, PinValue))
			{
				UE_LOG(LogGlobeNiagara, Error, TEXT("%s: could not format static switch '%s'"), *Call->GetFunctionName(), SwitchName);
				bOk = false;
				return;
			}
			Pin->Modify();
			Pin->DefaultValue = PinValue;
			Call->MarkNodeRequiresSynchronization(TEXT("Static switch set by FGlobeNiagaraGenerator"), true);
		}

		/** Links a module input to a User parameter (the stack's "Link Inputs > User" action). */
		void Link(UNiagaraNodeFunctionCall* Call, const TCHAR* InputName, const FNiagaraVariable& User)
		{
			const UNiagaraScriptVariable* Input = FindModuleInput(Call, InputName);
			if (!Call || !Input)
			{
				UE_LOG(LogGlobeNiagara, Error, TEXT("%s: input '%s' not found"), Call ? *Call->GetFunctionName() : TEXT("?"), InputName);
				bOk = false;
				return;
			}
			const FNiagaraParameterHandle Handle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
				FNiagaraParameterHandle(Input->Variable.GetName()), Call);
			UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				*Call, Handle, Input->Variable.GetType(), Input->Metadata.GetVariableGuid(), FGuid());
			if (OverridePin.LinkedTo.Num() == 0)
			{
				FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(OverridePin, User, KnownUserParams, ENiagaraDefaultMode::FailIfPreviouslyNotSet);
			}
		}

		/** Local value for a rapid-iteration input (what typing a number into the stack does). */
		template <typename T>
		void SetLiteral(UNiagaraNodeFunctionCall* Call, const TCHAR* InputName, ENiagaraScriptUsage Usage, const T& Value)
		{
			const UNiagaraScriptVariable* Input = FindModuleInput(Call, InputName);
			UNiagaraScript* Script = Data->GetScript(Usage, FGuid());
			if (!Call || !Input || !Script)
			{
				UE_LOG(LogGlobeNiagara, Error, TEXT("Cannot set literal %s"), InputName);
				bOk = false;
				return;
			}
			const FNiagaraParameterHandle Handle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
				FNiagaraParameterHandle(Input->Variable.GetName()), Call);
			FNiagaraVariable Aliased(Input->Variable.GetType(), Handle.GetParameterHandleString());
			FNiagaraVariable RapidIteration = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(Aliased, *UniqueEmitterName, Usage);
			RapidIteration.AllocateData();
			RapidIteration.SetValue(Value);
			Script->Modify();
			Script->RapidIterationParameters.SetParameterData(RapidIteration.GetData(), RapidIteration, true);
			Script->RapidIterationParameters.ParameterGuidMapping.Add(RapidIteration, Input->Metadata.GetVariableGuid());
		}

		/**
		 * Sets a module input to a one-line HLSL expression (the stack's "New Expression" action), e.g.
		 * "User.PrismRadius * 1.1". Namespaced names (Particles., Emitter., User., Engine.) read the parameter map.
		 * FNiagaraStackGraphUtilities::SetCustomExpressionForFunctionInput is not exported from NiagaraEditor, so this
		 * builds the same node: a dynamic-input Custom HLSL node with a parameter map input and one typed output,
		 * fed by the previous stack node and wired into the input's override pin.
		 */
		void SetExpression(UNiagaraNodeFunctionCall* Call, const TCHAR* InputName, const FString& Hlsl)
		{
			const UNiagaraScriptVariable* Input = FindModuleInput(Call, InputName);
			if (!Call || !Input)
			{
				UE_LOG(LogGlobeNiagara, Error, TEXT("%s: input '%s' not found for expression"), Call ? *Call->GetFunctionName() : TEXT("?"), InputName);
				bOk = false;
				return;
			}
			const FNiagaraParameterHandle Handle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
				FNiagaraParameterHandle(Input->Variable.GetName()), Call);
			UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
				*Call, Handle, Input->Variable.GetType(), Input->Metadata.GetVariableGuid(), FGuid());
			UEdGraphNode* OverrideNode = OverridePin.GetOwningNode(); // the stack's override Parameter Map Set node
			UEdGraphPin* OverrideMapIn = nullptr;
			for (UEdGraphPin* Pin : OverrideNode ? OverrideNode->Pins : TArray<UEdGraphPin*>())
			{
				if (Pin->Direction == EGPD_Input && UEdGraphSchema_Niagara::PinToTypeDefinition(Pin) == FNiagaraTypeDefinition::GetParameterMapDef())
				{
					OverrideMapIn = Pin;
					break;
				}
			}
			if (OverridePin.LinkedTo.Num() > 0 || !OverrideMapIn || OverrideMapIn->LinkedTo.Num() == 0)
			{
				UE_LOG(LogGlobeNiagara, Error, TEXT("%s: cannot set an expression on '%s' (input already set or stack graph unexpected)"), *Call->GetFunctionName(), InputName);
				bOk = false;
				return;
			}

			UEdGraph* NodeGraph = OverrideNode->GetGraph();
			NodeGraph->Modify();
			FGraphNodeCreator<UNiagaraNodeCustomHlsl> Creator(*NodeGraph);
			UNiagaraNodeCustomHlsl* Expression = Creator.CreateNode(false);
			// What InitAsCustomHlslDynamicInput sets up: usage, and the signature its pins are allocated from.
			Expression->ScriptUsage = ENiagaraScriptUsage::DynamicInput;
			Expression->Signature.Inputs = { FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Map")) };
			Expression->Signature.Outputs = { FNiagaraVariable(Input->Variable.GetType(), TEXT("CustomHLSLOutput")) };
			static FStrProperty* CodeProperty = FindFProperty<FStrProperty>(UNiagaraNodeCustomHlsl::StaticClass(), TEXT("CustomHlsl"));
			CodeProperty->SetPropertyValue_InContainer(Expression, Hlsl);
			Creator.Finalize();

			UEdGraphPin* MapIn = nullptr;
			UEdGraphPin* ValueOut = nullptr;
			for (UEdGraphPin* Pin : Expression->Pins)
			{
				if (Pin->Direction == EGPD_Input && Pin->PinName == TEXT("Map"))
				{
					MapIn = Pin;
				}
				else if (Pin->Direction == EGPD_Output && Pin->PinName == TEXT("CustomHLSLOutput"))
				{
					ValueOut = Pin;
				}
			}
			if (!MapIn || !ValueOut)
			{
				UE_LOG(LogGlobeNiagara, Error, TEXT("%s: expression node for '%s' has no Map/CustomHLSLOutput pins"), *Call->GetFunctionName(), InputName);
				bOk = false;
				return;
			}
			MapIn->MakeLinkTo(OverrideMapIn->LinkedTo[0]);
			ValueOut->MakeLinkTo(&OverridePin);
			Expression->SetEnabledState(OverrideNode->GetDesiredEnabledState(), OverrideNode->HasUserSetTheEnabledState());
			Expression->MarkNodeRequiresSynchronization(TEXT("Expression set by FGlobeNiagaraGenerator"), true);
		}

		/** A "Set Parameters" module (the stack's + > Set new or existing parameter) with an expression per target. */
		UNiagaraNodeFunctionCall* AddSet(ENiagaraScriptUsage Usage, std::initializer_list<TPair<FNiagaraVariable, FString>> Targets)
		{
			UNiagaraNodeOutput* Output = Graph->FindEquivalentOutputNode(Usage);
			if (!Output)
			{
				bOk = false;
				return nullptr;
			}
			TArray<FNiagaraVariable> Vars;
			TArray<FString> Defaults;
			for (const TPair<FNiagaraVariable, FString>& Target : Targets)
			{
				Vars.Add(Target.Key);
				Defaults.AddDefaulted();
			}
			UNiagaraNodeAssignment* Node = FNiagaraStackGraphUtilities::AddParameterModuleToStack(Vars, *Output, INDEX_NONE, Defaults);
			for (const TPair<FNiagaraVariable, FString>& Target : Targets)
			{
				SetExpression(Node, *Target.Key.GetName().ToString(), Target.Value);
			}
			return Node;
		}
	};

	void AddUserParameter(UNiagaraSystem* System, const FNiagaraVariable& Var, TFunctionRef<void(FNiagaraVariable&)> SetDefault, TSet<FNiagaraVariableBase>& Known)
	{
		FNiagaraVariable Default = Var;
		Default.AllocateData();
		SetDefault(Default);
		FNiagaraUserRedirectionParameterStore& Store = System->GetExposedParameters();
		Store.AddParameter(Default, true, false);
		Store.SetParameterData(Default.GetData(), Default, true);
		Known.Add(Var);
	}

	bool ReportCompile(UNiagaraSystem* System)
	{
		bool bClean = true;
		auto Check = [&bClean](UNiagaraScript* Script, const FString& Label)
		{
			if (!Script)
			{
				return;
			}
			const ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
			const FString& Error = Script->GetVMExecutableData().ErrorMsg;
			const bool bFailed = Status == ENiagaraScriptCompileStatus::NCS_Error || Status == ENiagaraScriptCompileStatus::NCS_Unknown;
			UE_LOG(LogGlobeNiagara, Display, TEXT("  %s: %s%s%s"), *Label, *StaticEnum<ENiagaraScriptCompileStatus>()->GetNameStringByValue((int64)Status),
				Error.IsEmpty() ? TEXT("") : TEXT(" - "), *Error);
			for (const FNiagaraCompileEvent& Event : Script->GetVMExecutableData().LastCompileEvents)
			{
				if (Event.Severity == FNiagaraCompileEventSeverity::Error || Event.Severity == FNiagaraCompileEventSeverity::Warning)
				{
					UE_LOG(LogGlobeNiagara, Display, TEXT("     %s: %s"), Event.Severity == FNiagaraCompileEventSeverity::Error ? TEXT("error") : TEXT("warning"), *Event.Message);
				}
			}
			bClean &= !bFailed;
		};
		Check(System->GetSystemSpawnScript(), TEXT("SystemSpawn"));
		Check(System->GetSystemUpdateScript(), TEXT("SystemUpdate"));
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData())
			{
				Check(EmitterData->SpawnScriptProps.Script, Handle.GetName().ToString() + TEXT(".ParticleSpawn"));
				Check(EmitterData->UpdateScriptProps.Script, Handle.GetName().ToString() + TEXT(".ParticleUpdate"));
			}
		}
		return bClean;
	}
}

namespace
{
	UMaterialInterface* LoadMaterialInstance(const TCHAR* Name)
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), GlobeVFX::Assets::MaterialFolder, Name, Name);
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Path);
		UE_CLOG(!Material, LogGlobeNiagara, Error, TEXT("Missing %s. Run AIGlobe.GenerateMaterials first."), *Path);
		return Material;
	}

	/** Finds or creates a system asset. An existing one is emptied (emitters and user parameters) and rebuilt in
	 *  place, so actor references stay valid. */
	UNiagaraSystem* PrepareSystem(const TCHAR* Name, bool& bOutCreated)
	{
		UNiagaraSystem* System = GlobeAssetUtils::FindOrCreateAsset<UNiagaraSystem>(GlobeVFX::Assets::NiagaraFolder, Name, bOutCreated);
		if (!System)
		{
			return nullptr;
		}
		if (bOutCreated)
		{
			UNiagaraSystemFactoryNew::InitializeSystem(System, true);
			return System;
		}
		GlobeAssetUtils::CloseEditorsFor(System);
		System->Modify();
		TSet<FGuid> OldHandles;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			OldHandles.Add(Handle.GetId());
		}
		System->RemoveEmitterHandlesById(OldHandles);
		TArray<FNiagaraVariable> OldUserParams;
		System->GetExposedParameters().GetParameters(OldUserParams);
		for (const FNiagaraVariable& Var : OldUserParams)
		{
			System->GetExposedParameters().RemoveParameter(Var);
		}
		return System;
	}

	/** Adds a CPU, local-space emitter from Niagara's Minimal template (EmitterState, InitializeParticle,
	 *  ParticleState, sprite renderer) and prepares a stack builder for it. */
	bool AddMinimalEmitter(UNiagaraSystem* System, const TCHAR* Name, const TSet<FNiagaraVariableBase>& Known, FStackBuilder& OutStack)
	{
		UNiagaraEmitter* Template = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
		if (!Template)
		{
			UE_LOG(LogGlobeNiagara, Error, TEXT("Niagara Minimal emitter template not found."));
			return false;
		}
		const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *Template, Template->GetExposedVersion().VersionGuid, true);
		FNiagaraEmitterHandle* Handle = System->GetEmitterHandles().FindByPredicate([&HandleId](const FNiagaraEmitterHandle& H) { return H.GetId() == HandleId; });
		FVersionedNiagaraEmitterData* Data = Handle ? Handle->GetEmitterData() : nullptr;
		UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		if (!Source || !Source->NodeGraph)
		{
			UE_LOG(LogGlobeNiagara, Error, TEXT("Failed to add emitter %s to %s"), Name, *System->GetPathName());
			return false;
		}
		Handle->SetName(FName(Name), *System);
		Data->SimTarget = ENiagaraSimTarget::CPUSim; // A few hundred to a few thousand particles: CPU is cheap.
		Data->bLocalSpace = true;                     // Particles ride along with the globe's transform.

		OutStack.System = System;
		OutStack.Data = Data;
		OutStack.Graph = Source->NodeGraph;
		OutStack.Emitter = Handle->GetInstance();
		OutStack.UniqueEmitterName = Handle->GetUniqueInstanceName();
		OutStack.KnownUserParams = Known;
		return true;
	}

	/** Points every sprite renderer of the emitter at the material (optionally bound to a User parameter). */
	void SetSpriteMaterial(FVersionedNiagaraEmitterData* Data, UMaterialInterface* Material, const FNiagaraVariable* UserBinding)
	{
		for (UNiagaraRendererProperties* Renderer : Data->GetRenderers())
		{
			if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
			{
				Sprite->Modify();
				Sprite->Material = Material;
				if (UserBinding)
				{
					Sprite->MaterialUserParamBinding.Parameter = *UserBinding;
				}
				Sprite->SortMode = ENiagaraSortMode::None; // Additive: order-independent.
			}
		}
	}

	bool CompileAndSave(UNiagaraSystem* System, bool bCreated, bool bStackOk)
	{
		if (!bStackOk)
		{
			UE_LOG(LogGlobeNiagara, Error, TEXT("%s stack setup failed; not saving."), *System->GetName());
			return false;
		}
		System->RequestCompile(false);
		System->WaitForCompilationComplete(false, false);
		UE_LOG(LogGlobeNiagara, Display, TEXT("%s %s:"), bCreated ? TEXT("Created") : TEXT("Rebuilt"), *System->GetPathName());
		const bool bCompiled = ReportCompile(System);
		return GlobeAssetUtils::SaveAsset(System) && bCompiled;
	}
}

static bool GenerateSurfaceSystem()
{
	using namespace GlobeVFX;

	UMaterialInterface* SpriteMaterial = LoadMaterialInstance(Assets::ParticleInstanceName);
	if (!SpriteMaterial)
	{
		return false;
	}

	bool bCreated = false;
	UNiagaraSystem* System = PrepareSystem(Assets::SurfaceSystemName, bCreated);
	if (!System)
	{
		return false;
	}

	// --- User parameters (C++ sets these; see AGlobeVFXActor) ---
	const FNiagaraTypeDefinition Float = FNiagaraTypeDefinition::GetFloatDef();
	const FNiagaraTypeDefinition Color = FNiagaraTypeDefinition::GetColorDef();
	TSet<FNiagaraVariableBase> Known;
	auto AddFloat = [&](const TCHAR* Name, float Value)
	{
		AddUserParameter(System, UserVar(Float, Name), [Value](FNiagaraVariable& V) { V.SetValue(Value); }, Known);
	};
	AddFloat(NiagaraParams::SurfaceRadius, 1110.0f);
	AddFloat(NiagaraParams::SurfaceHeightVariation, 0.0f);
	AddFloat(NiagaraParams::SphereSurfaceDistribution, 1.0f);
	AddFloat(NiagaraParams::GlobalEnergy, 0.2f);
	AddFloat(NiagaraParams::ParticleIntensity, 1.0f);
	AddFloat(NiagaraParams::SpawnRate, 250.0f);
	AddFloat(NiagaraParams::ParticleLifetime, 4.0f);
	AddFloat(NiagaraParams::SpriteSize, 14.0f);
	AddFloat(NiagaraParams::FlowSpeed, 15.0f);
	AddFloat(NiagaraParams::BackSideVisibility, 0.35f);
	AddFloat(NiagaraParams::LandMaskStrength, 0.0f);
	AddFloat(NiagaraParams::CenterClearAmount, 0.0f);
	AddFloat(NiagaraParams::CenterClearThreshold, 0.9f);
	AddFloat(NiagaraParams::CenterClearSoftness, 0.1f);
	AddUserParameter(System, UserVar(Color, NiagaraParams::GlowColor),
		[](FNiagaraVariable& V) { V.SetValue(FLinearColor(0.15f, 0.75f, 1.0f, 1.0f)); }, Known);

	// Sprite material as a User parameter, so the actor can switch between the depth-tested and on-top variants.
	const FNiagaraVariable SpriteMaterialVar(FNiagaraTypeDefinition(UMaterialInterface::StaticClass()), FName(FString(TEXT("User.")) + NiagaraParams::SpriteMaterial));
	System->GetExposedParameters().AddParameter(SpriteMaterialVar, true, false);
	System->GetExposedParameters().SetUObject(SpriteMaterial, SpriteMaterialVar);

	// --- Emitter from the Minimal template (EmitterState, InitializeParticle, ParticleState, Sprite renderer) ---
	FStackBuilder Stack;
	if (!AddMinimalEmitter(System, EmitterName, Known, Stack))
	{
		return false;
	}

	// Emitter update: continuous spawn keeps coverage steady (no burst-then-gap as lifetimes expire).
	UNiagaraNodeFunctionCall* SpawnRate = Stack.AddModule(TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"), ENiagaraScriptUsage::EmitterUpdateScript);
	Stack.Link(SpawnRate, TEXT("SpawnRate"), UserVar(Float, NiagaraParams::SpawnRate));

	// Particle spawn.
	UNiagaraNodeFunctionCall* Init = Stack.FindModule(TEXT("InitializeParticle"));
	Stack.SetStatic(Init, TEXT("Lifetime Mode"), TEXT("Direct Set"));
	Stack.Link(Init, TEXT("Lifetime"), UserVar(Float, NiagaraParams::ParticleLifetime));
	Stack.SetStatic(Init, TEXT("Color Mode"), TEXT("Direct Set"));
	Stack.Link(Init, TEXT("Color"), UserVar(Color, NiagaraParams::GlowColor));
	Stack.SetStatic(Init, TEXT("Sprite Size Mode"), TEXT("Uniform"));
	Stack.Link(Init, TEXT("Uniform Sprite Size"), UserVar(Float, NiagaraParams::SpriteSize));
	Stack.SetStatic(Init, TEXT("Write Material Random"), TEXT("true")); // Per-point twinkle seed in the material.

	UNiagaraNodeFunctionCall* Shape = Stack.AddModule(TEXT("/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation"), ENiagaraScriptUsage::ParticleSpawnScript);
	Stack.SetStatic(Shape, TEXT("Shape Primitive"), TEXT("Sphere"));
	Stack.SetStatic(Shape, TEXT("Surface Only"), TEXT("true"));
	Stack.Link(Shape, TEXT("Sphere Radius"), UserVar(Float, NiagaraParams::SurfaceRadius));
	// V2 spheres sample the volume unless this is 1 (the Surface Only switch does not govern spheres).
	Stack.Link(Shape, TEXT("Sphere Surface Distribution"), UserVar(Float, NiagaraParams::SphereSurfaceDistribution));
	// Surface band: Shape Location grows it inward from Sphere Radius, thickness normalised to that radius
	// (its defaults). AGlobeVFXActor passes the outer radius and variation / radius accordingly.
	Stack.Link(Shape, TEXT("Surface Only Band Thickness"), UserVar(Float, NiagaraParams::SurfaceHeightVariation));

	// Particle update: gentle tangential flow around the globe axis. Over one lifetime the drift is a
	// few degrees of arc, so points stay on the shell (radial Euler drift ~ v^2 t^2 / 2r, well under 1 cm).
	UNiagaraNodeFunctionCall* Vortex = Stack.AddModule(TEXT("/Niagara/Modules/Update/Velocity/VortexVelocity.VortexVelocity"), ENiagaraScriptUsage::ParticleUpdateScript);
	Stack.Link(Vortex, TEXT("Velocity Amount"), UserVar(Float, NiagaraParams::FlowSpeed));
	Stack.SetLiteral(Vortex, TEXT("Vortex Axis"), ENiagaraScriptUsage::ParticleUpdateScript, FVector3f(0.0f, 0.0f, 1.0f));

	Stack.AddModule(TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"), ENiagaraScriptUsage::ParticleUpdateScript);

	UNiagaraNodeFunctionCall* DynParams = Stack.AddModule(TEXT("/Niagara/Modules/Update/Material/DynamicMaterialParameters.DynamicMaterialParameters"), ENiagaraScriptUsage::ParticleUpdateScript);
	Stack.SetStatic(DynParams, TEXT("Write Parameter Index 0"), TEXT("Float"));
	Stack.SetLiteral(DynParams, TEXT("Index0Float0Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index0Float1Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index0Float2Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index0Float3Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.Link(DynParams, TEXT("Index 0 Param 1"), UserVar(Float, NiagaraParams::ParticleIntensity));
	Stack.Link(DynParams, TEXT("Index 0 Param 2"), UserVar(Float, NiagaraParams::GlobalEnergy));
	Stack.Link(DynParams, TEXT("Index 0 Param 3"), UserVar(Float, NiagaraParams::BackSideVisibility));
	Stack.Link(DynParams, TEXT("Index 0 Param 4"), UserVar(Float, NiagaraParams::LandMaskStrength));

	// Dynamic Parameter 1 = (CenterClearAmount, CenterClearThreshold, CenterClearSoftness).
	Stack.SetStatic(DynParams, TEXT("Write Parameter Index 1"), TEXT("Float"));
	Stack.SetLiteral(DynParams, TEXT("Index1Float0Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index1Float1Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index1Float2Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index1Float3Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(false));
	Stack.Link(DynParams, TEXT("Index 1 Param 1"), UserVar(Float, NiagaraParams::CenterClearAmount));
	Stack.Link(DynParams, TEXT("Index 1 Param 2"), UserVar(Float, NiagaraParams::CenterClearThreshold));
	Stack.Link(DynParams, TEXT("Index 1 Param 3"), UserVar(Float, NiagaraParams::CenterClearSoftness));

	SetSpriteMaterial(Stack.Data, SpriteMaterial, &SpriteMaterialVar);
	return CompileAndSave(System, bCreated, Stack.bOk);
}

/**
 * NS_GlobeCloudWave: the ring's mist layer. (The thin luminous waveform core is a procedural band mesh with
 * M_GlobeCloudCore on AGlobeVFXActor, which is smoother and cheaper than ribbons.)
 *   Emitter Update : EmitterState, SpawnRate(User.CloudDensity)
 *   Particle Spawn : InitializeParticle(User.MistLifetime, User.GlowColor, User.MistSize),
 *                    ShapeLocation(torus: User.RingRadius, User.RingThickness)
 *   Particle Update: ParticleState, VortexVelocity(User.MistSwirl, about Z), CurlNoiseForce(User.MistTurbulence,
 *                    User.MistNoiseFrequency), Drag(User.MistDrag), SolveForcesAndVelocity,
 *                    DynamicMaterialParameters(Index 0 = RingIntensity, AudioEnergy)
 *   Renderer       : Sprite, MI_GlobeMist
 */
static bool GenerateCloudWaveSystem()
{
	using namespace GlobeVFX;

	UMaterialInterface* MistMaterial = LoadMaterialInstance(Assets::MistInstanceName);
	if (!MistMaterial)
	{
		return false;
	}

	bool bCreated = false;
	UNiagaraSystem* System = PrepareSystem(Assets::CloudWaveSystemName, bCreated);
	if (!System)
	{
		return false;
	}

	const FNiagaraTypeDefinition Float = FNiagaraTypeDefinition::GetFloatDef();
	const FNiagaraTypeDefinition Color = FNiagaraTypeDefinition::GetColorDef();
	TSet<FNiagaraVariableBase> Known;
	auto AddFloat = [&](const TCHAR* Name, float Value)
	{
		AddUserParameter(System, UserVar(Float, Name), [Value](FNiagaraVariable& V) { V.SetValue(Value); }, Known);
	};
	// Defaults suit an ~11 m globe; AGlobeVFXActor overwrites all of them from its fitted sphere and settings.
	AddFloat(CloudParams::AudioEnergy, 0.0f);
	AddFloat(CloudParams::RingIntensity, 1.0f);
	AddFloat(CloudParams::RingRadius, 1300.0f);
	AddFloat(CloudParams::RingThickness, 60.0f);
	AddFloat(CloudParams::CloudDensity, 60.0f);
	AddFloat(CloudParams::MistSize, 90.0f);
	AddFloat(CloudParams::MistLifetime, 3.0f);
	AddFloat(CloudParams::MistTurbulence, 40.0f);
	AddFloat(CloudParams::MistNoiseFrequency, 0.004f);
	AddFloat(CloudParams::MistDrag, 1.5f);
	AddFloat(CloudParams::MistSwirl, 25.0f);
	AddFloat(CloudParams::RingClearAmount, 0.0f);
	AddFloat(CloudParams::RingClearThreshold, 0.8f);
	AddUserParameter(System, UserVar(Color, NiagaraParams::GlowColor),
		[](FNiagaraVariable& V) { V.SetValue(FLinearColor(0.2f, 0.75f, 1.0f, 1.0f)); }, Known);

	FStackBuilder Stack;
	if (!AddMinimalEmitter(System, TEXT("Mist"), Known, Stack))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* SpawnRate = Stack.AddModule(TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"), ENiagaraScriptUsage::EmitterUpdateScript);
	Stack.Link(SpawnRate, TEXT("SpawnRate"), UserVar(Float, CloudParams::CloudDensity));

	UNiagaraNodeFunctionCall* Init = Stack.FindModule(TEXT("InitializeParticle"));
	Stack.SetStatic(Init, TEXT("Lifetime Mode"), TEXT("Direct Set"));
	Stack.Link(Init, TEXT("Lifetime"), UserVar(Float, CloudParams::MistLifetime));
	Stack.SetStatic(Init, TEXT("Color Mode"), TEXT("Direct Set"));
	Stack.Link(Init, TEXT("Color"), UserVar(Color, NiagaraParams::GlowColor));
	Stack.SetStatic(Init, TEXT("Sprite Size Mode"), TEXT("Uniform"));
	Stack.Link(Init, TEXT("Uniform Sprite Size"), UserVar(Float, CloudParams::MistSize));
	Stack.SetStatic(Init, TEXT("Write Material Random"), TEXT("true"));

	// Spawn inside a thin torus around the equator (the component carries position, tilt and globe rotation).
	UNiagaraNodeFunctionCall* Shape = Stack.AddModule(TEXT("/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation"), ENiagaraScriptUsage::ParticleSpawnScript);
	Stack.SetStatic(Shape, TEXT("Shape Primitive"), TEXT("Torus"));
	Stack.Link(Shape, TEXT("Large Radius"), UserVar(Float, CloudParams::RingRadius));
	Stack.Link(Shape, TEXT("Handle Radius"), UserVar(Float, CloudParams::RingThickness));

	// Living motion: slow swirl along the ring, curl-noise turbulence (energy raises it), drag keeps the mist
	// from travelling far so the ring stays recognisable.
	UNiagaraNodeFunctionCall* Vortex = Stack.AddModule(TEXT("/Niagara/Modules/Update/Velocity/VortexVelocity.VortexVelocity"), ENiagaraScriptUsage::ParticleUpdateScript);
	Stack.Link(Vortex, TEXT("Velocity Amount"), UserVar(Float, CloudParams::MistSwirl));
	Stack.SetLiteral(Vortex, TEXT("Vortex Axis"), ENiagaraScriptUsage::ParticleUpdateScript, FVector3f(0.0f, 0.0f, 1.0f));

	UNiagaraNodeFunctionCall* Curl = Stack.AddModule(TEXT("/Niagara/Modules/Update/Forces/V2/CurlNoiseForce.CurlNoiseForce"), ENiagaraScriptUsage::ParticleUpdateScript);
	Stack.Link(Curl, TEXT("Noise Strength"), UserVar(Float, CloudParams::MistTurbulence));
	Stack.Link(Curl, TEXT("Noise Frequency"), UserVar(Float, CloudParams::MistNoiseFrequency));
	Stack.SetStatic(Curl, TEXT("Noise Quality / Cost"), TEXT("Baked (Low)"));

	UNiagaraNodeFunctionCall* Drag = Stack.AddModule(TEXT("/Niagara/Modules/Update/Forces/Drag.Drag"), ENiagaraScriptUsage::ParticleUpdateScript);
	Stack.Link(Drag, TEXT("Drag"), UserVar(Float, CloudParams::MistDrag));

	Stack.AddModule(TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity"), ENiagaraScriptUsage::ParticleUpdateScript);

	UNiagaraNodeFunctionCall* DynParams = Stack.AddModule(TEXT("/Niagara/Modules/Update/Material/DynamicMaterialParameters.DynamicMaterialParameters"), ENiagaraScriptUsage::ParticleUpdateScript);
	Stack.SetStatic(DynParams, TEXT("Write Parameter Index 0"), TEXT("Float"));
	Stack.SetLiteral(DynParams, TEXT("Index0Float0Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index0Float1Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index0Float2Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.SetLiteral(DynParams, TEXT("Index0Float3Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
	Stack.Link(DynParams, TEXT("Index 0 Param 1"), UserVar(Float, CloudParams::RingIntensity));
	Stack.Link(DynParams, TEXT("Index 0 Param 2"), UserVar(Float, CloudParams::AudioEnergy));
	Stack.Link(DynParams, TEXT("Index 0 Param 3"), UserVar(Float, CloudParams::RingClearAmount));
	Stack.Link(DynParams, TEXT("Index 0 Param 4"), UserVar(Float, CloudParams::RingClearThreshold));

	SetSpriteMaterial(Stack.Data, MistMaterial, nullptr);
	return CompileAndSave(System, bCreated, Stack.bOk);
}

/**
 * NS_GlobeDataLink: one curved data link (UGlobeDataLinksComponent spawns one component per active item).
 * Every emitter samples the same cubic Bezier (User.LinkP0 anchor -> User.LinkP3 endpoint) in component space:
 *   Stream   : many small particles, T = normalized age, so they flow from the anchor to the endpoint.
 *   Filament : a few faint, slow particles on three lanes (M_GlobeLink mode 1) that read as guide lines.
 *   Node     : overlapping pulsing glow sprites at T = User.NodeT (the endpoint).
 * The renderers read the sampled curve position (Particles.SampleBezierSpline.SplinePosition), so no particle
 * is moved from C++; C++ only sets User parameters.
 */
static bool GenerateDataLinkSystem()
{
	using namespace GlobeVFX;

	UMaterialInterface* LinkMaterial = LoadMaterialInstance(Assets::LinkInstanceName);
	if (!LinkMaterial)
	{
		return false;
	}

	bool bCreated = false;
	UNiagaraSystem* System = PrepareSystem(Assets::DataLinkSystemName, bCreated);
	if (!System)
	{
		return false;
	}

	const FNiagaraTypeDefinition Float = FNiagaraTypeDefinition::GetFloatDef();
	const FNiagaraTypeDefinition Color = FNiagaraTypeDefinition::GetColorDef();
	const FNiagaraTypeDefinition Vec3 = FNiagaraTypeDefinition::GetVec3Def();
	TSet<FNiagaraVariableBase> Known;
	auto AddFloat = [&](const TCHAR* Name, float Value)
	{
		AddUserParameter(System, UserVar(Float, Name), [Value](FNiagaraVariable& V) { V.SetValue(Value); }, Known);
	};
	auto AddVector = [&](const TCHAR* Name, const FVector3f& Value)
	{
		AddUserParameter(System, UserVar(Vec3, Name), [Value](FNiagaraVariable& V) { V.SetValue(Value); }, Known);
	};
	// Defaults draw a sample link for an ~11 m globe; UGlobeDataLinksComponent overwrites them every frame.
	AddVector(LinkParams::P0, FVector3f(600.0f, 0.0f, 0.0f));
	AddVector(LinkParams::P1, FVector3f(0.0f, -1500.0f, 900.0f));
	AddVector(LinkParams::P2, FVector3f(-1200.0f, -2000.0f, 1200.0f));
	AddVector(LinkParams::P3, FVector3f(-1500.0f, -2200.0f, 1300.0f));
	AddUserParameter(System, UserVar(Color, LinkParams::LinkColor),
		[](FNiagaraVariable& V) { V.SetValue(FLinearColor(0.15f, 0.85f, 1.0f, 1.0f)); }, Known);
	AddFloat(LinkParams::LinkIntensity, 1.0f);
	AddFloat(LinkParams::OccluderRadius, 1140.0f);
	AddFloat(LinkParams::StreamRate, 60.0f);
	AddFloat(LinkParams::StreamLifetime, 2.5f);
	AddFloat(LinkParams::StreamSize, 14.0f);
	AddFloat(LinkParams::StreamWidth, 22.0f);
	AddFloat(LinkParams::FilamentRate, 30.0f);
	AddFloat(LinkParams::FilamentLifetime, 6.0f);
	AddFloat(LinkParams::FilamentSize, 6.0f);
	AddFloat(LinkParams::FilamentSpread, 30.0f);
	AddFloat(LinkParams::NodeRate, 3.0f);
	AddFloat(LinkParams::NodeLifetime, 1.2f);
	AddFloat(LinkParams::NodeSize, 90.0f);
	AddFloat(LinkParams::NodeT, 1.0f);
	AddFloat(LinkParams::NodeWidth, 0.0f);
	AddFloat(LinkParams::ModeStream, 0.0f);
	AddFloat(LinkParams::ModeFilament, 1.0f);
	AddFloat(LinkParams::ModeNode, 2.0f);
	AddFloat(LinkParams::ModeCountry, 3.0f);
	AddFloat(LinkParams::ModeLoop, 4.0f);
	AddFloat(LinkParams::CountryRate, 0.0f); // off until a card supplies country colours
	AddFloat(LinkParams::CountryLifetime, 2.5f);
	AddFloat(LinkParams::CountrySize, 12.0f);
	AddFloat(LinkParams::CountrySpread, 60.0f);
	AddFloat(LinkParams::CountryIntensity, 1.0f);
	AddFloat(LinkParams::CountryColorCount, 2.0f);
	auto AddColor = [&](const TCHAR* Name, const FLinearColor& Value)
	{
		AddUserParameter(System, UserVar(Color, Name), [Value](FNiagaraVariable& V) { V.SetValue(Value); }, Known);
	};
	AddColor(LinkParams::CountryColorA, FLinearColor(1.0f, 0.1f, 0.12f, 1.0f));
	AddColor(LinkParams::CountryColorB, FLinearColor(0.1f, 0.35f, 1.0f, 1.0f));
	AddColor(LinkParams::CountryColorC, FLinearColor(0.1f, 0.85f, 0.35f, 1.0f));
	// Node behind the card centre, and the loop around the card (off until a card is placed).
	AddVector(LinkParams::NodePosition, FVector3f(-1700.0f, -2200.0f, 1300.0f));
	AddFloat(LinkParams::NodeIntensity, 1.0f);
	AddVector(LinkParams::CardCenter, FVector3f(-1700.0f, -2200.0f, 1300.0f));
	AddVector(LinkParams::CardHalfRight, FVector3f(0.0f, -250.0f, 0.0f));
	AddVector(LinkParams::CardHalfUp, FVector3f(0.0f, 0.0f, 150.0f));
	AddFloat(LinkParams::LoopStartAngle, 0.0f);
	AddFloat(LinkParams::LoopRate, 0.0f);
	AddFloat(LinkParams::LoopLifetime, 1.6f);
	AddFloat(LinkParams::LoopSize, 12.0f);
	AddFloat(LinkParams::LoopJitter, 6.0f);
	AddFloat(LinkParams::LoopIntensity, 1.0f);
	AddFloat(LinkParams::LoopRoundness, 7.0f);
	AddFloat(LinkParams::LoopTravel, 1.0f);

	const FNiagaraVariable NormalizedAge(Float, TEXT("Particles.NormalizedAge"));

	enum class EPath : uint8 { Curve, Node, CardLoop };
	enum class EColors : uint8 { Link, Country, CountryAndLink };
	struct FLinkEmitter
	{
		const TCHAR* Name;
		const TCHAR* Rate;
		const TCHAR* Lifetime;
		const TCHAR* Size;
		const TCHAR* Width;
		const TCHAR* Mode;
		EPath Path;
		bool bFlowAlongCurve; // Curve: T = normalized age; otherwise T = User.NodeT
		EColors Colors;
		const TCHAR* IntensityExpression; // nullptr = User.LinkIntensity
	};
	const FLinkEmitter Emitters[] = {
		{ TEXT("Stream"), LinkParams::StreamRate, LinkParams::StreamLifetime, LinkParams::StreamSize, LinkParams::StreamWidth, LinkParams::ModeStream, EPath::Curve, true, EColors::Link, nullptr },
		{ TEXT("Filament"), LinkParams::FilamentRate, LinkParams::FilamentLifetime, LinkParams::FilamentSize, LinkParams::FilamentSpread, LinkParams::ModeFilament, EPath::Curve, true, EColors::Link, nullptr },
		{ TEXT("Node"), LinkParams::NodeRate, LinkParams::NodeLifetime, LinkParams::NodeSize, LinkParams::NodeWidth, LinkParams::ModeNode, EPath::Node, false, EColors::Link,
			TEXT("User.LinkIntensity * User.NodeIntensity") },
		{ TEXT("CountryFlow"), LinkParams::CountryRate, LinkParams::CountryLifetime, LinkParams::CountrySize, LinkParams::CountrySpread, LinkParams::ModeCountry, EPath::Curve, true, EColors::Country,
			TEXT("User.LinkIntensity * User.CountryIntensity") },
		{ TEXT("CardLoop"), LinkParams::LoopRate, LinkParams::LoopLifetime, LinkParams::LoopSize, LinkParams::LoopJitter, LinkParams::ModeLoop, EPath::CardLoop, false, EColors::CountryAndLink,
			TEXT("User.LinkIntensity * User.LoopIntensity") },
	};
	// Deterministic 0..1 per particle (x - floor(x): the CPU VM's frac keeps the sign of negative values).
	auto Hash = [](float A, float B)
	{
		const FString Id = TEXT("(((float)Particles.UniqueID - floor((float)Particles.UniqueID / 7919.0) * 7919.0) + 1.0)");
		return FString::Printf(TEXT("(sin(%s * %.4f + %.4f) * 43758.5453 - floor(sin(%s * %.4f + %.4f) * 43758.5453))"), *Id, A, B, *Id, A, B);
	};
	const FString CountryPick = FString::Printf(TEXT("floor(%s * clamp(User.CountryColorCount, 1.0, 3.0))"), *Hash(12.9898f, 3.1f));
	const FString CountryColor = FString::Printf(TEXT("lerp(lerp(User.CountryColorA, User.CountryColorB, step(0.5, %s)), User.CountryColorC, step(1.5, %s))"), *CountryPick, *CountryPick);
	// Card loop: the country colours plus the link colour (white); with no country colours only the link colour.
	const FString LoopPick = FString::Printf(TEXT("floor(%s * (User.CountryColorCount + 1.0))"), *Hash(45.164f, 7.7f));
	const FString LoopColor = FString::Printf(TEXT("lerp(lerp(lerp(User.CountryColorA, User.CountryColorB, step(0.5, %s)), User.CountryColorC, step(1.5, %s)), User.LinkColor, step(User.CountryColorCount - 0.5, %s))"),
		*LoopPick, *LoopPick, *LoopPick);
	// Rounded rectangle as a superellipse (|x|^n + |y|^n = 1) in the camera plane around the card.
	const FString LoopPosition = TEXT(
		"User.CardCenter"
		" + User.CardHalfRight * (cos(Particles.LoopAngle) / max(abs(cos(Particles.LoopAngle)), 0.0001) * pow(abs(cos(Particles.LoopAngle)), 2.0 / max(User.LoopRoundness, 2.0)))"
		" + User.CardHalfUp * (sin(Particles.LoopAngle) / max(abs(sin(Particles.LoopAngle)), 0.0001) * pow(abs(sin(Particles.LoopAngle)), 2.0 / max(User.LoopRoundness, 2.0)))");
	const FNiagaraVariable LoopAngle(Float, TEXT("Particles.LoopAngle"));
	const FNiagaraVariable Position(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Particles.Position"));

	bool bOk = true;
	for (const FLinkEmitter& Def : Emitters)
	{
		FStackBuilder Stack;
		if (!AddMinimalEmitter(System, Def.Name, Known, Stack))
		{
			return false;
		}

		UNiagaraNodeFunctionCall* SpawnRate = Stack.AddModule(TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"), ENiagaraScriptUsage::EmitterUpdateScript);
		Stack.Link(SpawnRate, TEXT("SpawnRate"), UserVar(Float, Def.Rate));

		UNiagaraNodeFunctionCall* Init = Stack.FindModule(TEXT("InitializeParticle"));
		Stack.SetStatic(Init, TEXT("Lifetime Mode"), TEXT("Direct Set"));
		Stack.Link(Init, TEXT("Lifetime"), UserVar(Float, Def.Lifetime));
		Stack.SetStatic(Init, TEXT("Color Mode"), TEXT("Direct Set"));
		Stack.Link(Init, TEXT("Color"), UserVar(Color, LinkParams::LinkColor));
		Stack.SetStatic(Init, TEXT("Sprite Size Mode"), TEXT("Uniform"));
		Stack.Link(Init, TEXT("Uniform Sprite Size"), UserVar(Float, Def.Size));
		Stack.SetStatic(Init, TEXT("Write Material Random"), TEXT("true")); // lateral offset / lane seed
		if (Def.Colors != EColors::Link)
		{
			Stack.AddSet(ENiagaraScriptUsage::ParticleSpawnScript, { { FNiagaraVariable(Color, TEXT("Particles.Color")), Def.Colors == EColors::Country ? CountryColor : LoopColor } });
		}

		if (Def.Path == EPath::Curve)
		{
			// The curve, re-sampled every frame so the link follows the endpoints as the globe or camera moves.
			// SampleBezierSpline naming: P1 / P2 are the curve's start / end points, P0 / P3 their handles.
			UNiagaraNodeFunctionCall* Spline = Stack.AddModule(TEXT("/Niagara/Modules/Update/Splines/SampleBezierSpline.SampleBezierSpline"), ENiagaraScriptUsage::ParticleUpdateScript);
			Stack.Link(Spline, TEXT("P1 - Start Point"), UserVar(Vec3, LinkParams::P0));
			Stack.Link(Spline, TEXT("P0 - Start Handle"), UserVar(Vec3, LinkParams::P1));
			Stack.Link(Spline, TEXT("P3 - End Handle"), UserVar(Vec3, LinkParams::P2));
			Stack.Link(Spline, TEXT("P2 - End Point"), UserVar(Vec3, LinkParams::P3));
			Stack.Link(Spline, TEXT("T - Position Along Spline"), Def.bFlowAlongCurve ? NormalizedAge : UserVar(Float, LinkParams::NodeT));
		}
		else if (Def.Path == EPath::Node)
		{
			// The node glows behind the card's centre (followed every frame).
			Stack.AddSet(ENiagaraScriptUsage::ParticleSpawnScript, { { Position, TEXT("User.NodePosition") } });
			Stack.AddSet(ENiagaraScriptUsage::ParticleUpdateScript, { { Position, TEXT("User.NodePosition") } });
		}
		else
		{
			// Leaves the link's end (the card's inner edge, LoopStartAngle) and runs around the card, half the
			// particles each way, meeting on the far side (LoopTravel = 1) as they fade.
			Stack.AddSet(ENiagaraScriptUsage::ParticleSpawnScript, {
				{ FNiagaraVariable(Float, TEXT("Particles.LoopDirection")), FString::Printf(TEXT("(step(0.5, %s) * 2.0 - 1.0)"), *Hash(78.233f, 1.3f)) },
				{ FNiagaraVariable(Float, TEXT("Particles.LoopSeed")), Hash(39.425f, 5.2f) } });
			const FString Angle = TEXT("User.LoopStartAngle + Particles.LoopDirection * (Particles.LoopSeed * 0.12 + Particles.NormalizedAge * User.LoopTravel) * 3.14159265");
			Stack.AddSet(ENiagaraScriptUsage::ParticleSpawnScript, { { LoopAngle, Angle } });
			Stack.AddSet(ENiagaraScriptUsage::ParticleSpawnScript, { { Position, LoopPosition } });
			Stack.AddSet(ENiagaraScriptUsage::ParticleUpdateScript, { { LoopAngle, Angle } });
			Stack.AddSet(ENiagaraScriptUsage::ParticleUpdateScript, { { Position, LoopPosition } });
		}

		UNiagaraNodeFunctionCall* DynParams = Stack.AddModule(TEXT("/Niagara/Modules/Update/Material/DynamicMaterialParameters.DynamicMaterialParameters"), ENiagaraScriptUsage::ParticleUpdateScript);
		Stack.SetStatic(DynParams, TEXT("Write Parameter Index 0"), TEXT("Float"));
		Stack.SetLiteral(DynParams, TEXT("Index0Float0Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
		Stack.SetLiteral(DynParams, TEXT("Index0Float1Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
		Stack.SetLiteral(DynParams, TEXT("Index0Float2Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
		Stack.SetLiteral(DynParams, TEXT("Index0Float3Write"), ENiagaraScriptUsage::ParticleUpdateScript, FNiagaraBool(true));
		if (Def.IntensityExpression)
		{
			Stack.SetExpression(DynParams, TEXT("Index 0 Param 1"), Def.IntensityExpression);
		}
		else
		{
			Stack.Link(DynParams, TEXT("Index 0 Param 1"), UserVar(Float, LinkParams::LinkIntensity));
		}
		Stack.Link(DynParams, TEXT("Index 0 Param 2"), UserVar(Float, Def.Width));
		Stack.Link(DynParams, TEXT("Index 0 Param 3"), UserVar(Float, LinkParams::OccluderRadius));
		Stack.Link(DynParams, TEXT("Index 0 Param 4"), UserVar(Float, Def.Mode));

		SetSpriteMaterial(Stack.Data, LinkMaterial, nullptr);
		if (Def.Path == EPath::Curve)
		{
			for (UNiagaraRendererProperties* Renderer : Stack.Data->GetRenderers())
			{
				if (UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(Renderer))
				{
					Sprite->PositionBinding.SetValue(FName(TEXT("Particles.SampleBezierSpline.SplinePosition")), Stack.Emitter, ENiagaraRendererSourceDataMode::Particles);
					Sprite->PostEditChange();
				}
			}
		}
		bOk &= Stack.bOk;
	}

	return CompileAndSave(System, bCreated, bOk);
}

/**
 * NS_GlobePrisms: sparkles on the prisms ribbon (the PS3-XMB-style sheets of UGlobePrismRibbonComponent).
 *   Emitter Update : EmitterState, SpawnRate(User.SparkleRate x on/off ramp)
 *   Particle Spawn : InitializeParticle(User.SparkleLifetime, material random), then a random sheet (PrismSeed, same
 *                    seeds as the mesh), a random point across the band (PrismV) and around it (PrismU), and a small
 *                    lift above / below the sheet
 *   Particle Update: PrismU drifts around the ring; Position = the sheet surface at (U, V) from
 *                    GlobeVFX::PrismParams::WaveHeightHlsl with User.RibbonClock (the same clock the actor pushes to
 *                    the sheet material), so the sparkles ride the moving sheets; size, colour, Dynamic Parameters
 *   Renderer       : Sprite, MI_GlobePrismSprite (dust mode: soft dot, life fade, twinkle; hidden in front of the globe
 *                    unless User.FrontVisibility > 0)
 * CPU sim, local space (the component sits at the globe centre with the ribbon's orientation).
 */
static bool GeneratePrismsSystem()
{
	using namespace GlobeVFX;
	namespace PP = GlobeVFX::PrismParams;

	UMaterialInterface* SpriteMaterial = LoadMaterialInstance(Assets::PrismSpriteInstanceName);
	if (!SpriteMaterial)
	{
		return false;
	}

	bool bCreated = false;
	UNiagaraSystem* System = PrepareSystem(Assets::PrismsSystemName, bCreated);
	if (!System)
	{
		return false;
	}

	const FNiagaraTypeDefinition Float = FNiagaraTypeDefinition::GetFloatDef();
	const FNiagaraTypeDefinition Color = FNiagaraTypeDefinition::GetColorDef();
	const FNiagaraTypeDefinition Vec2 = FNiagaraTypeDefinition::GetVec2Def();
	const FNiagaraTypeDefinition Vec4 = FNiagaraTypeDefinition::GetVec4Def();
	TSet<FNiagaraVariableBase> Known;
	auto AddFloat = [&](const TCHAR* Name, float Value)
	{
		AddUserParameter(System, UserVar(Float, Name), [Value](FNiagaraVariable& V) { V.SetValue(Value); }, Known);
	};
	auto AddColor = [&](const TCHAR* Name, const FLinearColor& Value)
	{
		AddUserParameter(System, UserVar(Color, Name), [Value](FNiagaraVariable& V) { V.SetValue(Value); }, Known);
	};
	// Defaults suit the ~11.5 m showcase globe; AGlobeVFXActor overwrites all of them.
	AddFloat(PP::RibbonClock, 0.0f);
	AddFloat(PP::RibbonInner, 1290.0f);
	AddFloat(PP::RibbonOuter, 2010.0f);
	AddFloat(PP::RibbonAmp, 180.0f);
	AddFloat(PP::RibbonLayers, 5.0f);
	AddFloat(PP::RibbonFlutter, 0.6f);
	AddFloat(PP::RibbonFlutterClock, 0.0f);
	AddFloat(PP::OccluderRadius, 1150.0f);
	AddFloat(PP::BackSideVisibility, 0.3f);
	AddFloat(PP::FrontVisibility, 0.0f);
	AddFloat(PP::PrismIntensity, 1.0f);
	AddColor(PP::BlueColor, FLinearColor(0.08f, 0.55f, 1.0f, 1.0f));
	AddColor(PP::GoldColor, FLinearColor(1.0f, 0.68f, 0.3f, 1.0f));
	AddFloat(PP::PrismRamp, 1.0f);
	AddFloat(PP::RampMinScale, 0.15f);
	AddFloat(PP::SparkleRate, 60.0f);
	AddFloat(PP::SparkleLifetime, 3.0f);
	AddFloat(PP::SparkleSize, 14.0f);
	AddFloat(PP::SparkleDrift, 0.01f);
	AddFloat(PP::SparkleIntensity, 1.2f);

	auto P = [](const TCHAR* Name, const FNiagaraTypeDefinition& Type) { return FNiagaraVariable(Type, FName(FString(TEXT("Particles.")) + Name)); };
	// Cheap deterministic 0..1 hash: h - floor(h) with h = sin(x * a + b) * 43758.5453 (not frac(): the CPU VM's frac
	// keeps the sign of negative values).
	auto Hash = [](const TCHAR* X, float A, float B) { return FString::Printf(TEXT("(sin(%s * %.4f + %.4f) * 43758.5453 - floor(sin(%s * %.4f + %.4f) * 43758.5453))"), X, A, B, X, A, B); };
	const TCHAR* Id = TEXT("(((float)Particles.UniqueID - floor((float)Particles.UniqueID / 7919.0) * 7919.0) + 1.0)");
	const ENiagaraScriptUsage Spawn = ENiagaraScriptUsage::ParticleSpawnScript;
	const ENiagaraScriptUsage Update = ENiagaraScriptUsage::ParticleUpdateScript;

	FStackBuilder Stack;
	if (!AddMinimalEmitter(System, TEXT("Sparkles"), Known, Stack))
	{
		return false;
	}

	UNiagaraNodeFunctionCall* SpawnRate = Stack.AddModule(TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate"), ENiagaraScriptUsage::EmitterUpdateScript);
	Stack.SetExpression(SpawnRate, TEXT("SpawnRate"), TEXT("User.SparkleRate * saturate(User.PrismRamp * 1.5)"));

	UNiagaraNodeFunctionCall* Init = Stack.FindModule(TEXT("InitializeParticle"));
	Stack.SetStatic(Init, TEXT("Lifetime Mode"), TEXT("Direct Set"));
	Stack.Link(Init, TEXT("Lifetime"), UserVar(Float, PP::SparkleLifetime));
	Stack.SetStatic(Init, TEXT("Write Material Random"), TEXT("true")); // twinkle seed

	Stack.AddSet(Spawn, {
		{ P(TEXT("PrismU"), Float), Hash(Id, 12.9898f, 1.7f) },
		{ P(TEXT("PrismV"), Float), FString::Printf(TEXT("lerp(0.1, 0.95, %s)"), *Hash(Id, 78.233f, 4.1f)) },
		{ P(TEXT("PrismLayer"), Float), FString::Printf(TEXT("min(floor(%s * User.RibbonLayers), max(User.RibbonLayers - 1.0, 0.0))"), *Hash(Id, 39.425f, 2.3f)) },
		{ P(TEXT("PrismLift"), Float), FString::Printf(TEXT("(%s - 0.5) * 0.3"), *Hash(Id, 93.989f, 5.9f)) },
		{ P(TEXT("PrismSizeSeed"), Float), Hash(Id, 27.17f, 0.6f) } });
	Stack.AddSet(Spawn, {
		// Same seeds as UGlobePrismRibbonComponent::LayerSeed, so each sparkle rides one of the mesh's sheets; the last
		// sheet is the gold one (as in M_GlobePrismSheet).
		{ P(TEXT("PrismSeed"), Float), TEXT("(0.37 + Particles.PrismLayer * 0.618034 - floor(0.37 + Particles.PrismLayer * 0.618034))") },
		{ P(TEXT("Color"), Color), TEXT("float4(lerp(User.BlueColor.rgb, User.GoldColor.rgb, step(User.RibbonLayers - 1.5, Particles.PrismLayer) * step(1.5, User.RibbonLayers)), 1.0)") } });

	// The sheet surface at (U, V), with the ramp shrinking the band toward its middle radius and lowering the waves.
	const FString S = TEXT("lerp(User.RampMinScale, 1.0, User.PrismRamp)");
	const FString A = FString::Printf(TEXT("(User.RibbonAmp * %s)"), *S);
	const FString Radius = FString::Printf(TEXT("(0.5 * (User.RibbonInner + User.RibbonOuter) + (lerp(User.RibbonInner, User.RibbonOuter, Particles.PrismV) - 0.5 * (User.RibbonInner + User.RibbonOuter)) * %s)"), *S);
	const FString Height = PP::WaveHeightHlsl(TEXT("Particles.PrismU"), TEXT("Particles.PrismV"), TEXT("User.RibbonClock"), TEXT("Particles.PrismSeed"), A, TEXT("User.RibbonFlutter"), TEXT("User.RibbonFlutterClock"));
	Stack.AddSet(Update, { { P(TEXT("PrismU"), Float), TEXT("Particles.PrismU + User.SparkleDrift * Engine.DeltaTime") } });
	Stack.AddSet(Update, {
		{ P(TEXT("Position"), FNiagaraTypeDefinition::GetPositionDef()), FString::Printf(TEXT("float3(cos(6.2831853 * Particles.PrismU) * %s, sin(6.2831853 * Particles.PrismU) * %s, %s + Particles.PrismLift * %s)"),
			*Radius, *Radius, *Height, *A) },
		{ P(TEXT("SpriteSize"), Vec2), FString::Printf(TEXT("float2(1.0, 1.0) * User.SparkleSize * lerp(0.4, 1.6, Particles.PrismSizeSeed * Particles.PrismSizeSeed) * %s"), *S) },
		// Dynamic Parameter 0 = (dust mode, intensity, occluder radius, back-side visibility); 2 = (front visibility).
		{ P(TEXT("DynamicMaterialParameter"), Vec4), TEXT("float4(0.0, User.SparkleIntensity * User.PrismIntensity * saturate(User.PrismRamp * 4.0), User.OccluderRadius, User.BackSideVisibility)") },
		{ P(TEXT("DynamicMaterialParameter2"), Vec4), TEXT("float4(User.FrontVisibility, 0.0, 0.0, 0.0)") } });

	SetSpriteMaterial(Stack.Data, SpriteMaterial, nullptr);
	return CompileAndSave(System, bCreated, Stack.bOk);
}

bool FGlobeNiagaraGenerator::GenerateDataLink()
{
	return GenerateDataLinkSystem();
}

bool FGlobeNiagaraGenerator::GeneratePrisms()
{
	return GeneratePrismsSystem();
}

bool FGlobeNiagaraGenerator::Generate()
{
	const bool bSurface = GenerateSurfaceSystem();
	const bool bCloud = GenerateCloudWaveSystem();
	const bool bLinks = GenerateDataLinkSystem();
	const bool bPrisms = GeneratePrismsSystem();
	return bSurface && bCloud && bLinks && bPrisms;
}
