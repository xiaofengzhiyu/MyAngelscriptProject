#include "EditorMenuExtensions/ScriptEditorMenuExtension.h"

#include "AngelscriptEngine.h"
#include "ClassGenerator/AngelscriptClassGenerator.h"
#include "ClassGenerator/ASClass.h"
#include "ContentBrowserDelegates.h"
#include "ContentBrowserModule.h"
#include "LevelEditor.h"
#include "ToolMenus.h"
#include "UObject/UObjectIterator.h"
TArray<UScriptEditorMenuExtension::FRegisteredExtender> UScriptEditorMenuExtension::RegisteredExtensions;

void UScriptEditorMenuExtension::UnregisterExtensions()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Unregister all previous extensions
	for (auto Extension : RegisteredExtensions)
	{
		switch (Extension.Location)
		{
		case EScriptEditorMenuExtensionLocation::LevelViewport_ContextMenu:
			LevelEditorModule.GetAllLevelViewportContextMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_DragDropContextMenu:
			LevelEditorModule.GetAllLevelViewportDragDropContextMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelViewportMenuExtender_SelectedObjects& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_OptionsMenu:
			LevelEditorModule.GetAllLevelViewportOptionsMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_ShowMenu:
			LevelEditorModule.GetAllLevelViewportShowMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_ViewMenu:
			LevelEditorModule.GetAllLevelEditorToolbarViewMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_BuildMenu:
			LevelEditorModule.GetAllLevelEditorToolbarBuildMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CompileMenu:
			LevelEditorModule.GetAllLevelEditorToolbarCompileMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_SourceControlMenu:
			LevelEditorModule.GetAllLevelEditorToolbarSourceControlMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CreateMenu:
			LevelEditorModule.GetAllLevelEditorToolbarCreateMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_PlayMenu:
			LevelEditorModule.GetAllLevelEditorToolbarPlayMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_BlueprintsMenu:
			LevelEditorModule.GetAllLevelEditorToolbarBlueprintsMenuExtenders().RemoveAll(
				[&](const TSharedPtr<FExtender>& Extender)
				{
					return Extender == Extension.Extender;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CinematicsMenu:
			LevelEditorModule.GetAllLevelEditorToolbarCinematicsMenuExtenders().RemoveAll(
				[&](const TSharedPtr<FExtender>& Extender)
				{
					return Extender == Extension.Extender;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_LevelMenu:
			LevelEditorModule.GetAllLevelEditorLevelMenuExtenders().RemoveAll(
				[&](const FLevelEditorModule::FLevelEditorMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetContextMenu:
			ContentBrowserModule.GetAllAssetContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender_SelectedPaths& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_PathViewContextMenu:
			ContentBrowserModule.GetAllPathViewContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender_SelectedPaths& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionListContextMenu:
			ContentBrowserModule.GetAllCollectionListContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionViewContextMenu:
			ContentBrowserModule.GetAllCollectionViewContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewContextMenu:
			ContentBrowserModule.GetAllAssetViewContextMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender_SelectedAssets& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewViewMenu:
			ContentBrowserModule.GetAllAssetViewViewMenuExtenders().RemoveAll(
				[&](const FContentBrowserMenuExtender& Value)
				{
					return Value.GetHandle() == Extension.DelegateHandle;
				}
			);
		break;
		case EScriptEditorMenuExtensionLocation::ToolMenu:
		{
			auto* Menu = UToolMenus::Get()->ExtendMenu(Extension.ExtensionPoint);
			Menu->RemoveSection(Extension.SectionName);
		}
		break;
		}
	}

	RegisteredExtensions.Reset();
}

void UScriptEditorMenuExtension::RegisterExtension(UScriptEditorMenuExtension* CDO)
{
	if (CDO == nullptr)
	{
		return;
	}

	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FRegisteredExtender Registered;
	Registered.Location = CDO->ExtensionMenu;
	Registered.ExtensionPoint = CDO->ExtensionPoint;

	switch (CDO->ExtensionMenu)
	{
	case EScriptEditorMenuExtensionLocation::LevelViewport_ContextMenu:
		LevelEditorModule.GetAllLevelViewportContextMenuExtenders().Add(
			FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors) -> TSharedRef<FExtender>
				{
					FExtenderSelection Selection;
					for (auto* Actor : SelectedActors)
					{
						Selection.SelectedObjects.Add(Actor);
					}
					return CDO->Extend(CommandList, Selection);
				}
			)
		);
		Registered.DelegateHandle = LevelEditorModule.GetAllLevelViewportContextMenuExtenders().Last().GetHandle();
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_DragDropContextMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelViewportMenuExtender_SelectedObjects::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> SelectedObjects) -> TSharedRef<FExtender>
			{
				FExtenderSelection Selection;
				Selection.SelectedObjects = SelectedObjects;
				return CDO->Extend(CommandList, Selection);
			}
		);
		LevelEditorModule.GetAllLevelViewportDragDropContextMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_OptionsMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelViewportOptionsMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_ShowMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelViewportShowMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_ViewMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelEditorToolbarViewMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_BuildMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelEditorToolbarBuildMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_CompileMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelEditorToolbarCompileMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_SourceControlMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelEditorToolbarSourceControlMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_CreateMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelEditorToolbarCreateMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_PlayMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelEditorToolbarPlayMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_BlueprintsMenu:
	{
		Registered.Extender = CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
		LevelEditorModule.GetAllLevelEditorToolbarBlueprintsMenuExtenders().Add(Registered.Extender);
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_CinematicsMenu:
	{
		Registered.Extender = CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
		LevelEditorModule.GetAllLevelEditorToolbarCinematicsMenuExtenders().Add(Registered.Extender);
	}
	break;
	case EScriptEditorMenuExtensionLocation::LevelViewport_LevelMenu:
	{
		auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
			[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
			{
				return CDO->Extend(CommandList, FExtenderSelection());
			}
		);
		LevelEditorModule.GetAllLevelEditorLevelMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetContextMenu:
	{
		auto Delegate = FContentBrowserMenuExtender_SelectedPaths::CreateLambda(
			[CDO](const TArray<FString>& Paths) -> TSharedRef<FExtender>
			{
				return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			}
		);
		ContentBrowserModule.GetAllAssetContextMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::ContentBrowser_PathViewContextMenu:
	{
		auto Delegate = FContentBrowserMenuExtender_SelectedPaths::CreateLambda(
			[CDO](const TArray<FString>& Paths) -> TSharedRef<FExtender>
			{
				return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			}
		);
		ContentBrowserModule.GetAllPathViewContextMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionListContextMenu:
	{
		auto Delegate = FContentBrowserMenuExtender::CreateLambda(
			[CDO]() -> TSharedRef<FExtender>
			{
				return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			}
		);
		ContentBrowserModule.GetAllCollectionListContextMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionViewContextMenu:
	{
		auto Delegate = FContentBrowserMenuExtender::CreateLambda(
			[CDO]() -> TSharedRef<FExtender>
			{
				return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			}
		);
		ContentBrowserModule.GetAllCollectionViewContextMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewContextMenu:
	{
		auto Delegate = FContentBrowserMenuExtender_SelectedAssets::CreateLambda(
			[CDO](const TArray<FAssetData>& Assets) -> TSharedRef<FExtender>
			{
				FExtenderSelection Selection;
				Selection.SelectedAssets = Assets;
				return CDO->Extend(MakeShared<FUICommandList>(), Selection);
			}
		);
		ContentBrowserModule.GetAllAssetViewContextMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewViewMenu:
	{
		auto Delegate = FContentBrowserMenuExtender::CreateLambda(
			[CDO]() -> TSharedRef<FExtender>
			{
				return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			}
		);
		ContentBrowserModule.GetAllAssetViewViewMenuExtenders().Add(Delegate);
		Registered.DelegateHandle = Delegate.GetHandle();
	}
	break;
	case EScriptEditorMenuExtensionLocation::ToolMenu:
	{
		Registered.SectionName = CDO->GetClass()->GetFName();

		auto* Menu = UToolMenus::Get()->ExtendMenu(Registered.ExtensionPoint);
		FText SectionHeader = CDO->MenuSectionHeader;
		if (SectionHeader.IsEmpty())
		{
			SectionHeader = CDO->GetClass()->GetDisplayNameText();
		}

		FToolMenuInsert InsertPosition;
		InsertPosition.Position = CDO->ToolMenuInsertType;
		InsertPosition.Name = CDO->ToolMenuInsertPosition;

		FToolMenuSection& Section = Menu->AddSection(Registered.SectionName, SectionHeader, InsertPosition);
		Section.Alignment = CDO->ToolMenuSectionAlign;
		Section.AddDynamicEntry(Registered.SectionName, FNewToolMenuSectionDelegate::CreateLambda([CDO, Menu](FToolMenuSection& DynamicSection)
		{
			const bool bIsMenu = (Menu->MenuType == EMultiBoxType::MenuBar) || (Menu->MenuType == EMultiBoxType::Menu);
			CDO->BuildToolMenuSection(DynamicSection, FExtenderSelection(), bIsMenu);
		}));
	}
	break;
	}

	RegisteredExtensions.Add(Registered);
}

void UScriptEditorMenuExtension::RegisterExtensions()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	// Register new extensions
	for (TObjectIterator<UASClass> ScriptClass; ScriptClass; ++ScriptClass)
	{
		if (!ScriptClass->IsChildOf(UScriptEditorMenuExtension::StaticClass()))
			continue;
		if (ScriptClass->HasAnyClassFlags(CLASS_Abstract))
			continue;
		if (ScriptClass->HasAnyClassFlags(CLASS_NewerVersionExists))
			continue;
		if (ScriptClass->ScriptTypePtr == nullptr)
			continue;

		auto* CDO = ScriptClass->GetDefaultObject<UScriptEditorMenuExtension>();

		FRegisteredExtender Registered;
		Registered.Location = CDO->ExtensionMenu;
		Registered.ExtensionPoint = CDO->ExtensionPoint;

		switch (CDO->ExtensionMenu)
		{
		case EScriptEditorMenuExtensionLocation::LevelViewport_ContextMenu:
			LevelEditorModule.GetAllLevelViewportContextMenuExtenders().Add(
				FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateLambda(
					[CDO](const TSharedRef<FUICommandList> CommandList, const TArray<AActor*> SelectedActors) -> TSharedRef<FExtender>
					{
						FExtenderSelection Selection;
						for (auto* Actor : SelectedActors)
							Selection.SelectedObjects.Add(Actor);
						return CDO->Extend(CommandList, Selection);
					}
				)
			);
			Registered.DelegateHandle = LevelEditorModule.GetAllLevelViewportContextMenuExtenders().Last().GetHandle();
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_DragDropContextMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelViewportMenuExtender_SelectedObjects::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList, const TArray<UObject*> SelectedObjects) -> TSharedRef<FExtender>
				{
					FExtenderSelection Selection;
					Selection.SelectedObjects = SelectedObjects;
					return CDO->Extend(CommandList, Selection);
				}
			);
			LevelEditorModule.GetAllLevelViewportDragDropContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_OptionsMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelViewportOptionsMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_ShowMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelViewportShowMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_ViewMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarViewMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_BuildMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarBuildMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CompileMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarCompileMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_SourceControlMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarSourceControlMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CreateMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarCreateMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_PlayMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorToolbarPlayMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_BlueprintsMenu:
		{
			Registered.Extender = CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			LevelEditorModule.GetAllLevelEditorToolbarBlueprintsMenuExtenders().Add(Registered.Extender);
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_CinematicsMenu:
		{
			Registered.Extender = CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
			LevelEditorModule.GetAllLevelEditorToolbarCinematicsMenuExtenders().Add(Registered.Extender);
		}
		break;
		case EScriptEditorMenuExtensionLocation::LevelViewport_LevelMenu:
		{
			auto Delegate = FLevelEditorModule::FLevelEditorMenuExtender::CreateLambda(
				[CDO](const TSharedRef<FUICommandList> CommandList) -> TSharedRef<FExtender>
				{
					return CDO->Extend(CommandList, FExtenderSelection());
				}
			);
			LevelEditorModule.GetAllLevelEditorLevelMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender_SelectedPaths::CreateLambda(
				[CDO](const TArray<FString>& Paths) -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllAssetContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_PathViewContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender_SelectedPaths::CreateLambda(
				[CDO](const TArray<FString>& Paths) -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllPathViewContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionListContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender::CreateLambda(
				[CDO]() -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllCollectionListContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_CollectionViewContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender::CreateLambda(
				[CDO]() -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllCollectionViewContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewContextMenu:
		{
			auto Delegate = FContentBrowserMenuExtender_SelectedAssets::CreateLambda(
				[CDO](const TArray<FAssetData>& Assets) -> TSharedRef<FExtender>
				{
					FExtenderSelection Selection;
					Selection.SelectedAssets = Assets;
					return CDO->Extend(MakeShared<FUICommandList>(), Selection);
				}
			);
			ContentBrowserModule.GetAllAssetViewContextMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ContentBrowser_AssetViewViewMenu:
		{
			auto Delegate = FContentBrowserMenuExtender::CreateLambda(
				[CDO]() -> TSharedRef<FExtender>
				{
					return CDO->Extend(MakeShared<FUICommandList>(), FExtenderSelection());
				}
			);
			ContentBrowserModule.GetAllAssetViewViewMenuExtenders().Add(Delegate);
			Registered.DelegateHandle = Delegate.GetHandle();
		}
		break;
		case EScriptEditorMenuExtensionLocation::ToolMenu:
		{
			Registered.SectionName = CDO->GetClass()->GetFName();

			auto* Menu = UToolMenus::Get()->ExtendMenu(Registered.ExtensionPoint);
			FText SectionHeader = CDO->MenuSectionHeader;
			if (SectionHeader.IsEmpty())
				SectionHeader = CDO->GetClass()->GetDisplayNameText();

			FToolMenuInsert InsertPosition;
			InsertPosition.Position = CDO->ToolMenuInsertType;
			InsertPosition.Name = CDO->ToolMenuInsertPosition;

			FToolMenuSection& Section = Menu->AddSection(Registered.SectionName, SectionHeader, InsertPosition);
			Section.Alignment = CDO->ToolMenuSectionAlign;
			Section.AddDynamicEntry(Registered.SectionName, FNewToolMenuSectionDelegate::CreateLambda([CDO, Menu](FToolMenuSection& DynamicSection)
			{
				bool bIsMenu = (Menu->MenuType == EMultiBoxType::MenuBar) || (Menu->MenuType == EMultiBoxType::Menu);
				CDO->BuildToolMenuSection(DynamicSection, FExtenderSelection(), bIsMenu);
			}));
		}
		break;
		}

		RegisteredExtensions.Add(Registered);
	}
}

