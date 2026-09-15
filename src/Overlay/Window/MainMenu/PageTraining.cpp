#include "MainMenuPages.h"

#include "Core/HotkeyManager.h"
#include "Core/interfaces.h"
#include "Core/Localization.h"
#include "Core/utils.h"
#include "Game/gamestates.h"
#include "Overlay/imgui_utils.h"
#include "Overlay/WindowContainer/WindowContainer.h"
#include "Overlay/Window/ScrWindow.h"

#include "imgui.h"

namespace MainMenu
{
	namespace
	{
		void OpenCbrDataManager(WindowContainer* container, int loadSlot)
		{
			if (!container)
				return;

			CbrInterface& cbr = g_interfaces.cbrInterface;
			cbr.windowLoadNr = loadSlot;
			cbr.windowReload = true;

			if (IWindow* manager = container->GetWindow(WindowType_CbrServer))
				manager->Open();
		}

		void DrawCbrPlayerControls(int playerIndex, bool mirrorMatch, WindowContainer* container)
		{
			CbrInterface& cbr = g_interfaces.cbrInterface;
			Player& player = playerIndex == 0 ? g_interfaces.player1 : g_interfaces.player2;
			Player& opponent = playerIndex == 0 ? g_interfaces.player2 : g_interfaces.player1;
			CbrData* data = cbr.getCbrData(playerIndex);
			AnnotatedReplay* annotated = cbr.getAnnotatedReplay(1 - playerIndex);

			if (!data || player.IsCharDataNullPtr() || opponent.IsCharDataNullPtr())
			{
				ImGui::TextDisabled("Character data is not ready yet.");
				return;
			}

			bool& recording = playerIndex == 0 ? cbr.Recording : cbr.RecordingP2;
			bool& replaying = playerIndex == 0 ? cbr.Replaying : cbr.ReplayingP2;
			bool& instantLearning = playerIndex == 0 ? cbr.instantLearning : cbr.instantLearningP2;

			ImGui::PushID(playerIndex);
			ImGui::Text("Replays: %d", data->getReplayCount());
			ImGui::Text("Frames recorded: %d", annotated ? annotated->getInputSize() : 0);
			ImGui::Text("Input: %d", playerIndex == 0 ? cbr.input : cbr.inputP2);

			const char* recordLabel = recording ? "Stop recording" : "Record";
			if (ImGui::Button(recordLabel, ImVec2(-1.0f, 0.0f)))
			{
				if (!recording)
				{
					cbr.EndCbrActivities(playerIndex);
					cbr.StartCbrRecording(
						player.GetData()->char_abbr,
						opponent.GetData()->char_abbr,
						player.GetData()->charIndex,
						opponent.GetData()->charIndex,
						playerIndex);
				}
				else
				{
					cbr.EndCbrActivities(playerIndex, true);
				}
			}

			const char* replayLabel = replaying ? "Stop replay" : "Replay AI";
			if (ImGui::Button(replayLabel, ImVec2(-1.0f, 0.0f)))
			{
				if (!replaying && data->getReplayCount() > 0)
				{
					cbr.EndCbrActivities(playerIndex);
					replaying = true;
				}
				else
				{
					cbr.EndCbrActivities(playerIndex);
				}
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Let the CBR AI take control using the data stored in this slot.");

			if (mirrorMatch)
			{
				const char* instantLabel = instantLearning ? "Stop instant learning" : "Instant learning";
				if (ImGui::Button(instantLabel, ImVec2(-1.0f, 0.0f)))
				{
					if (!instantLearning)
					{
						cbr.EndCbrActivities();
						cbr.StartCbrInstantLearning(
							player.GetData()->char_abbr,
							opponent.GetData()->char_abbr,
							player.GetData()->charIndex,
							opponent.GetData()->charIndex,
							playerIndex);
					}
					else
					{
						cbr.EndCbrActivities();
					}
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Learn from this player in real time while the AI controls the other side. Mirror matches only.");
			}
			else
			{
				ImGui::TextDisabled("Instant learning needs a mirror match.");
			}

			if (ImGui::Button("Delete range", ImVec2(-1.0f, 0.0f)))
			{
				cbr.EndCbrActivities();
				data->deleteReplays(cbr.deletionStart, cbr.deletionEnd);
			}
			if (ImGui::Button("Delete last", ImVec2(-1.0f, 0.0f)))
			{
				cbr.EndCbrActivities();
				data->deleteLastReplay();
			}

			if (ImGui::Button("Save", ImVec2(-1.0f, 0.0f)))
			{
				cbr.EndCbrActivities();
				data->setPlayerName(cbr.playerName);
				data->setCharName(player.GetData()->char_abbr);
				cbr.SaveCbrDataThreaded(*data, true);
			}
			if (ImGui::Button("Load by name", ImVec2(-1.0f, 0.0f)))
			{
				cbr.EndCbrActivities();
				cbr.LoadCbrData(cbr.playerName, player.GetData()->char_abbr, true, playerIndex);
			}
			if (ImGui::Button("Browse / load data...", ImVec2(-1.0f, 0.0f)))
			{
				cbr.EndCbrActivities();
				OpenCbrDataManager(container, playerIndex);
			}
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Open the CBR data manager and load or merge a saved AI file into this slot.");

			ImGui::PopID();
		}

		void DrawCbrSection(bool inTraining, WindowContainer* container)
		{
			CbrInterface& cbr = g_interfaces.cbrInterface;
			cbr.loadSettings(&cbr);

			Hint(L("CBR AI learns from recorded player behaviour and imitates it. Record examples, replay them as an AI, or use instant learning in a mirror match."));
			Hint(L("The menu is wired first. The CBR runtime input hooks are still being ported into the HaiKamDesu base, so these controls will not affect gameplay until that runtime wiring is finished."));

			if (ImGui::Button("CBR data manager..."))
				OpenCbrDataManager(container, -1);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Browse, filter, upload, download, and delete CBR data. Slot-specific load and merge buttons are available from each player column below.");

			bool settingsChanged = false;
			settingsChanged |= ImGui::Checkbox("Auto record myself", &cbr.autoRecordGameOwner);
			settingsChanged |= ImGui::Checkbox("Auto record opponents", &cbr.autoRecordAllOtherPlayers);
			settingsChanged |= ImGui::Checkbox("Auto upload own data", &cbr.autoUploadOwnData);
			settingsChanged |= ImGui::Checkbox("Auto save in lobby", &cbr.autoRecordConfirmation);
			if (settingsChanged)
				cbr.saveSettings();

			if (!inTraining)
			{
				Unavailable(L("Enter training mode to use the live CBR controls below."));
				return;
			}

			if (cbr.threadActiveCheck())
			{
				Unavailable(L("CBR data is currently being saved or loaded. Please wait."));
				return;
			}

			if (g_interfaces.player1.IsCharDataNullPtr() || g_interfaces.player2.IsCharDataNullPtr())
			{
				Unavailable(L("Waiting for both characters to be available."));
				return;
			}

			const bool mirrorMatch = g_interfaces.player1.GetData()->charIndex == g_interfaces.player2.GetData()->charIndex;

			if (ImGui::BeginTable("CBRPlayers", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame))
			{
				ImGui::TableNextColumn();
				ImGui::SeparatorText("Player 1 / Slot 1");
				DrawCbrPlayerControls(0, mirrorMatch, container);

				ImGui::TableNextColumn();
				ImGui::SeparatorText("Player 2 / Slot 2");
				DrawCbrPlayerControls(1, mirrorMatch, container);
				ImGui::EndTable();
			}

			ImGui::VerticalSpacing(6);
			if (ImGui::Button("Stop all CBR activity"))
				cbr.EndCbrActivities();

			ImGui::SameLine();
			if (ImGui::Button("Replay both"))
			{
				CbrData* p1 = cbr.getCbrData(0);
				CbrData* p2 = cbr.getCbrData(1);
				if (p1 && p2 && p1->getReplayCount() > 0 && p2->getReplayCount() > 0 && !cbr.Replaying && !cbr.ReplayingP2)
				{
					cbr.EndCbrActivities();
					cbr.Replaying = true;
					cbr.ReplayingP2 = true;
				}
				else
				{
					cbr.EndCbrActivities();
				}
			}

			ImGui::Text("Player name:");
			ImGui::SetNextItemWidth(220.0f);
			ImGui::InputText("##CBRPlayerName", cbr.playerName, IM_ARRAYSIZE(cbr.playerName));

			ImGui::Text("Replay deletion range:");
			ImGui::SetNextItemWidth(220.0f);
			ImGui::DragIntRange2("##CBRDeleteRange", &cbr.deletionStart, &cbr.deletionEnd, 1.0f, 0);

			const std::string state = cbr.WriteAiInterfaceState();
			if (!state.empty())
				ImGui::TextWrapped("%s", state.c_str());
		}
	}

	void DrawTrainingPage(const PageContext& ctx)
	{
		ScrWindow* scr = ctx.container->GetWindow<ScrWindow>(WindowType_Scr);
		if (!scr)
			return;

		const bool inTraining = g_gameVals.pGameMode && *g_gameVals.pGameMode == GameMode_Training;
		if (!inTraining)
		{
			Hint(L("Everything on this page needs training mode. It is all listed anyway so you know what is waiting for you there."));
			ImGui::VerticalSpacing(6);
		}

		// Two checkboxes; a category of its own would be all frame and no picture.
		Anchor(Training_Positions);
		scr->DrawPositionsBody();

		ImGui::VerticalSpacing(8);

		if (BeginSection(Training_Dummy, inTraining))
		{
			Hint(L("Pick one of the dummy's own moves and tell it when to use that move: after waking up, in a gap in your pressure, when it gets hit, or when it techs a throw."));
			scr->DrawDummyActionsBody();
		}

		if (BeginSection(Training_Slots, inTraining))
		{
			Hint(L("Record what you do into a slot, then have the dummy replay it. Four classic slots, plus the longer Unlimited Playback recorder."));
			scr->DrawRecordingSlotsBody();
		}

		GroupLabel(Training_SaveStates, inTraining);
		Hint(FormatText(L("Save the exact moment you are in and jump back to it later. Hotkeys: %s to save, %s to load.").c_str(),
			HotkeyManager::DisplayString(HotkeyManager::GetBinding(HotkeyManager::Hotkey_SaveState)).c_str(),
			HotkeyManager::DisplayString(HotkeyManager::GetBinding(HotkeyManager::Hotkey_LoadState)).c_str()));
		scr->DrawSaveStatesBody();

		ImGui::VerticalSpacing(4);
		GroupLabel(Training_Wakeup, inTraining);
		scr->DrawWakeupBody();

		ImGui::VerticalSpacing(8);
		if (BeginSection(Training_CbrAi, inTraining))
			DrawCbrSection(inTraining, ctx.container);

		ImGui::VerticalSpacing(8);
		ImGui::Separator();
		ImGui::VerticalSpacing(4);

		Anchor(Training_Tas);
		scr->DrawTasComboToolButton();
	}
}
