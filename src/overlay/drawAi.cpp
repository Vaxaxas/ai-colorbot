#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "imgui/imgui.h"
#include "app.h"
#include "util/otherTools.h"
#include "overlay/overlay.h"

void drawAi()
{
    ImGui::TextUnformatted("AI Model");
    auto availableModels = getAvailableModels();
    if (availableModels.empty())
    {
        ImGui::TextDisabled("No models found in the 'models' folder.");
    }
    else
    {
        int currentModel = 0;
        for (int i = 0; i < static_cast<int>(availableModels.size()); ++i)
            if (availableModels[i] == config.ai_model)
                currentModel = i;

        std::vector<const char*> items;
        items.reserve(availableModels.size());
        for (auto& m : availableModels) items.push_back(m.c_str());

        if (ImGui::Combo("Model", &currentModel, items.data(), static_cast<int>(items.size())))
        {
            config.ai_model = availableModels[currentModel];
            config.saveConfig();
            detector_model_changed.store(true);
        }
    }

    ImGui::Separator();

    ImGui::TextUnformatted("Postprocess Engine");
    static const char* ppOptions[] = { "yolo8", "yolo9", "yolo10", "yolo11", "yolo12", "yolo26" };
    int ppIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(ppOptions); ++i)
        if (config.postprocess == ppOptions[i])
            ppIndex = i;

    if (ImGui::Combo("Postprocess", &ppIndex, ppOptions, IM_ARRAYSIZE(ppOptions)))
    {
        config.postprocess = ppOptions[ppIndex];
        config.saveConfig();
        detector_model_changed.store(true);
    }

    int precIndex = (config.export_precision == 8 ? 0 : 1);
    static const char* precisionOptions[] = { "8", "16" };
    if (ImGui::Combo("Precision", &precIndex, precisionOptions, IM_ARRAYSIZE(precisionOptions)))
    {
        config.export_precision = (precIndex == 0 ? 8 : 16);
        config.saveConfig();
    }

    ImGui::Separator();

    ImGui::TextUnformatted("Detection Settings");
    ImGui::SliderFloat("Confidence Threshold", &config.confidence_threshold, 0.01f, 1.00f, "%.2f");
    ImGui::SliderFloat("NMS Threshold",         &config.nms_threshold,        0.01f, 1.00f, "%.2f");
    ImGui::SliderInt  ("Max Detections",        &config.max_detections,       1,     500);

    ImGui::Separator();

    ImGui::TextUnformatted("Per-Class Detection & Color Filters");
    auto getClassName = [&](int i) -> std::string {
        if (i == config.class_player) return "Player";
        if (i == config.class_head) return "Head";
        if (i == config.class_dead_body) return "Dead Body";
        if (i == config.class_bot) return "Bot";
        if (i == config.class_weapon) return "Weapon";
        if (i == config.class_outline) return "Outline";
        if (i == config.class_hideout_target_human) return "Target A";
        if (i == config.class_hideout_target_balls) return "Target B";
        if (i == config.class_smoke) return "Smoke";
        if (i == config.class_fire) return "Fire";
        if (i == config.class_third_person) return "Third Person";
        return "Class " + std::to_string(i);
    };

    int numClassesCount = static_cast<int>(config.detect.size());
    if (numClassesCount > 11) numClassesCount = 11;

    if (ImGui::BeginTable("ClassesTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
    {
        ImGui::TableSetupColumn("Enable",          ImGuiTableColumnFlags_WidthFixed,  60.0f);
        ImGui::TableSetupColumn("Class",           ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Color Filter On", ImGuiTableColumnFlags_WidthFixed,  80.0f);
        ImGui::TableHeadersRow();

        for (int i = 0; i < numClassesCount; ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool enabled = config.detect[i];
            if (ImGui::Checkbox(("##enable_" + std::to_string(i)).c_str(), &enabled))
            {
                config.detect[i] = enabled;
                if (!enabled) config.color[i] = false;
                config.saveConfig();
            }
            if (!config.detect[i]) ImGui::BeginDisabled();

            ImGui::TableSetColumnIndex(1);
            std::string displayName = getClassName(i);
            ImGui::TextUnformatted(displayName.c_str());

            ImGui::TableSetColumnIndex(2);
            bool colorVal = config.color[i];
            if (ImGui::Checkbox(("##color_" + std::to_string(i)).c_str(), &colorVal))
            {
                config.color[i] = colorVal;
                config.saveConfig();
            }

            if (!config.detect[i]) ImGui::EndDisabled();
        }

        ImGui::EndTable();
    }

    ImGui::Separator();

    ImGui::TextUnformatted("CUDA Options");
    ImGui::Checkbox("Use CUDA Graph",    &config.use_cuda_graph);
    ImGui::Checkbox("Use Pinned Memory", &config.use_pinned_memory);
}
