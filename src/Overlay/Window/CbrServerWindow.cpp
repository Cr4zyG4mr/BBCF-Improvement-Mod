#include "CbrServerWindow.h"

#include "CBR/CharacterStorage.h"
#include "CBR/CbrUtils.h"
#include "Core/interfaces.h"
#include "Core/utils.h"
#include "Game/gamestates.h"
#include "Overlay/imgui_utils.h"

#include <cstdio>
#include <cstring>
#include <string>

using json = nlohmann::json;

namespace
{
    json g_serverJsonData = nullptr;
    json g_localJsonData = nullptr;

    char g_uploader[128] = "";
    char g_playerName[128] = "";
    char g_playerCharacter[128] = "";
    char g_opponentCharacter[128] = "";
    char g_replayCountMin[128] = "";

    std::string JsonString(const json& row, const char* key)
    {
        if (!row.is_object())
            return "";
        const auto it = row.find(key);
        if (it == row.end() || !it->is_string())
            return "";
        return it->get<std::string>();
    }

    int JsonInt(const json& row, const char* key)
    {
        if (!row.is_object())
            return 0;
        const auto it = row.find(key);
        if (it == row.end() || !it->is_number_integer())
            return 0;
        return it->get<int>();
    }

    void SaveDownloadedData(const json& result)
    {
        if (result.is_null() || !result.is_object())
            return;

        const auto dataIt = result.find("data");
        if (dataIt == result.end() || !dataIt->is_string())
            return;

        CbrData cbrData = ConvertCbrDataFromBase64(dataIt->get<std::string>());
        g_interfaces.cbrInterface.SaveCbrDataThreaded(cbrData, true);
    }

    void DrawFilters()
    {
        ImGui::TextUnformatted("Filtering options");

        if (ImGui::BeginTable("CBRFilters", 5, ImGuiTableFlags_SizingStretchSame))
        {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Uploader");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##Uploader", g_uploader, IM_ARRAYSIZE(g_uploader));

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Player name");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##PlayerName", g_playerName, IM_ARRAYSIZE(g_playerName));

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Player character");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##PlayerCharacter", g_playerCharacter, IM_ARRAYSIZE(g_playerCharacter));

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Opponent character");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##OpponentCharacter", g_opponentCharacter, IM_ARRAYSIZE(g_opponentCharacter));

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Replay count");
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("##ReplayCount", g_replayCountMin, IM_ARRAYSIZE(g_replayCountMin));

            ImGui::EndTable();
        }
    }

    void DrawServerData()
    {
        CbrInterface& cbr = g_interfaces.cbrInterface;

        if (cbr.playerID.empty())
        {
            ImGui::TextDisabled("Log in to BlazBlue network mode to access the online CBR database.");
            setUpdateServerWindow(true);
            return;
        }

        json bufferData = threadDownloadFilteredData(
            updateServerWindow(true),
            g_uploader,
            g_playerName,
            g_playerCharacter,
            g_opponentCharacter,
            g_replayCountMin);
        if (!bufferData.is_null())
            g_serverJsonData = bufferData;

        if (ImGui::Button("Refresh server data"))
        {
            bufferData = threadDownloadFilteredData(
                true,
                g_uploader,
                g_playerName,
                g_playerCharacter,
                g_opponentCharacter,
                g_replayCountMin);
            if (!bufferData.is_null())
                g_serverJsonData = bufferData;
        }

        if (isCbrServerBusy() || cbr.threadActiveCheck())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Working...");
        }

        if (!g_serverJsonData.is_array())
            return;

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX;
        if (!ImGui::BeginTable("CBRServerData", 7, flags))
            return;

        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Uploader");
        ImGui::TableSetupColumn("Player name");
        ImGui::TableSetupColumn("Player character");
        ImGui::TableSetupColumn("Opponent character");
        ImGui::TableSetupColumn("Replays", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 155.0f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < g_serverJsonData.size(); ++i)
        {
            const json& row = g_serverJsonData[i];
            const std::string uploader = JsonString(row, "player_id");
            const std::string publicId = JsonString(row, "public_id");

            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%zu", i);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(uploader.c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(JsonString(row, "player_name").c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(JsonString(row, "player_character").c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(JsonString(row, "opponent_character").c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", JsonInt(row, "replay_count"));
            ImGui::TableNextColumn();

            if (ImGui::Button("Download"))
            {
                const json result = threadDownloadCharData(!isCbrServerActivityHappening(), publicId);
                SaveDownloadedData(result);
            }

            if (uploader == cbr.playerID || cbr.playerID == "KDing")
            {
                ImGui::SameLine();
                if (ImGui::Button("Delete"))
                {
                    threadDeleteCharData(true, publicId);
                    g_serverJsonData.erase(g_serverJsonData.begin() + static_cast<json::difference_type>(i));
                    setUpdateServerWindow(true);
                    ImGui::PopID();
                    break;
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    bool DrawLocalData()
    {
        CbrInterface& cbr = g_interfaces.cbrInterface;
        bool closeRequested = false;

        json bufferData = threadGetLocalMetadata(
            updateLocalWindow(true),
            g_playerName,
            g_playerCharacter,
            g_opponentCharacter,
            g_replayCountMin);
        if (!bufferData.is_null())
            g_localJsonData = bufferData;

        if (ImGui::Button("Refresh local data"))
        {
            bufferData = threadGetLocalMetadata(
                true,
                g_playerName,
                g_playerCharacter,
                g_opponentCharacter,
                g_replayCountMin);
            if (!bufferData.is_null())
                g_localJsonData = bufferData;
        }

        if (isCbrLocalDataUpdating() || cbr.threadActiveCheck())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Working...");
        }

        if (cbr.playerID.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("Log in to network mode to upload local data.");
        }

        if (!g_localJsonData.is_array())
            return false;

        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollX;
        if (!ImGui::BeginTable("CBRLocalData", 7, flags))
            return false;

        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 36.0f);
        ImGui::TableSetupColumn("Uploader");
        ImGui::TableSetupColumn("Player name");
        ImGui::TableSetupColumn("Player character");
        ImGui::TableSetupColumn("Opponent character");
        ImGui::TableSetupColumn("Replays", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, cbr.windowLoadNr >= 0 ? 285.0f : 150.0f);
        ImGui::TableHeadersRow();

        for (std::size_t i = 0; i < g_localJsonData.size(); ++i)
        {
            const json& row = g_localJsonData[i];
            const std::string path = JsonString(row, "path");

            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%zu", i);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(JsonString(row, "player_id").c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(JsonString(row, "player_name").c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(JsonString(row, "player_character").c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(JsonString(row, "opponent_character").c_str());
            ImGui::TableNextColumn(); ImGui::Text("%d", JsonInt(row, "replay_count"));
            ImGui::TableNextColumn();

            if (!cbr.playerID.empty())
            {
                if (ImGui::Button("Upload"))
                {
                    cbr.EndCbrActivities();
                    cbr.LoadAndUploadDataThreaded(path, true);
                }
                ImGui::SameLine();
            }

            bool rowDeleted = false;
            if (ImGui::Button("Delete"))
            {
                const bool removed = std::remove(path.c_str()) == 0;
                const std::string metadataPath = metaDataFilepathRename(path);
                std::remove(metadataPath.c_str());
                if (removed)
                {
                    g_localJsonData.erase(g_localJsonData.begin() + static_cast<json::difference_type>(i));
                    setUpdateLocalWindow(true);
                    rowDeleted = true;
                }
            }

            if (!rowDeleted && cbr.windowLoadNr >= 0)
            {
                ImGui::SameLine();
                if (ImGui::Button("Load"))
                {
                    cbr.LoadCbrData(path, true, cbr.windowLoadNr);
                    closeRequested = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Load + merge"))
                {
                    cbr.MergeCbrDataThreaded(path, true, cbr.windowLoadNr);
                    closeRequested = true;
                }
            }

            ImGui::PopID();
            if (rowDeleted || closeRequested)
                break;
        }

        ImGui::EndTable();
        return closeRequested;
    }
}

void CbrServerWindow::Draw()
{
    CbrInterface& cbr = g_interfaces.cbrInterface;

    if (cbr.playerID.empty())
        cbr.playerID = cbr.GetPlayerID();

    const bool supportedMatch = isInMatch() && g_gameVals.pGameMode &&
        (*g_gameVals.pGameMode == GameMode_Training ||
         *g_gameVals.pGameMode == GameMode_Versus ||
         *g_gameVals.pGameMode == GameMode_Online);
    if (!supportedMatch)
        cbr.windowLoadNr = -1;

    DrawImGuiSection();
}

void CbrServerWindow::DrawImGuiSection()
{
    CbrInterface& cbr = g_interfaces.cbrInterface;

    threadReturnCheck();
    SaveDownloadedData(threadDownloadCharData(false, ""));

    if (cbr.windowLoadNr == 0 && !g_interfaces.player1.IsCharDataNullPtr())
        strcpy_s(g_playerCharacter, sizeof(g_playerCharacter), g_interfaces.player1.GetData()->char_abbr);
    else if (cbr.windowLoadNr == 1 && !g_interfaces.player2.IsCharDataNullPtr())
        strcpy_s(g_playerCharacter, sizeof(g_playerCharacter), g_interfaces.player2.GetData()->char_abbr);

    DrawFilters();
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Server data"))
        DrawServerData();
    else
        setUpdateServerWindow(true);

    if (cbr.windowReload)
    {
        setUpdateLocalWindow(true);
        cbr.windowReload = false;
        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    }

    if (ImGui::CollapsingHeader("Local data"))
    {
        if (DrawLocalData())
            Close();
    }
    else
    {
        setUpdateLocalWindow(true);
    }
}

void CbrServerWindow::Update()
{
    g_interfaces.cbrInterface.clearThreads();

    if (!m_windowOpen)
    {
        endAllThreads();
        return;
    }

    IWindow::Update();
}
