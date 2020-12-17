#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#   define IMGUI_DEFINE_MATH_OPERATORS
#endif

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_te_perflog.h"
#include "imgui_te_internal.h"
#include "libs/Str/Str.h"
#include "libs/implot/implot.h"
#include "libs/implot/implot_internal.h"


using HashEntryFn = ImGuiID(*)(ImGuiPerflogEntry* entry);
using FormatEntryLabelFn = void(*)(ImGuiPerfLog* perflog, Str256f* result, ImGuiPerflogEntry* entry);

static int IMGUI_CDECL PerflogComparerByTimestampAndTestName(const void* lhs, const void* rhs)
{
    const ImGuiPerflogEntry* a = (const ImGuiPerflogEntry*)lhs;
    const ImGuiPerflogEntry* b = (const ImGuiPerflogEntry*)rhs;
    if ((ImS64)b->Timestamp - (ImS64)a->Timestamp < 0)
        return -1;
    if ((ImS64)b->Timestamp - (ImS64)a->Timestamp > 0)
        return +1;
    return strcmp(a->TestName, b->TestName);
}

static bool IsDateValid(const char* date)
{
    if (date[4] != '-' || date[7] != '-')
        return false;
    for (int i = 0; i < 10; i++)
    {
        if (i == 4 || i == 7)
            continue;
        if (date[i] < '0' || date[i] > '9')
            return false;
    }
    return true;
}

static ImGuiID PerflogHashTestName(ImGuiPerflogEntry* entry)
{
    return ImHashStr(entry->TestName);
}

static ImGuiID PerflogHashBuildInfo(ImGuiPerflogEntry* entry)
{
    return ImHashData(&entry->Timestamp, sizeof(entry->Timestamp));
}

static void PerflogFormatTestName(ImGuiPerfLog* perflog, Str256f* result, ImGuiPerflogEntry* entry)
{
    IM_UNUSED(perflog);
    result->set_ref(entry->TestName);
}

static void PerflogFormatBuildInfo(ImGuiPerfLog* perflog, Str256f* result, ImGuiPerflogEntry* entry)
{
    Str64f legend_format("x%%-%dd %%-%ds %%s %%-%ds %%-%ds %%s %%-%ds", perflog->_AlignStress, perflog->_AlignType, perflog->_AlignOs, perflog->_AlignCompiler, perflog->_AlignBranch);
    result->appendf(legend_format.c_str(), entry->PerfStressAmount, entry->BuildType, entry->Cpu, entry->OS, entry->Compiler, entry->Date, entry->GitBranchName);
}

// Copied from implot_demo.cpp and modified.
template <typename T>
int BinarySearch(const T* arr, int l, int r, const void* data, int (*compare)(const T&, const void*))
{
    if (r < l)
        return -1;
    int mid = l + (r - l) / 2;
    int cmp = compare(arr[mid], data);
    if (cmp == 0)
        return mid;
    if (cmp > 0)
        return BinarySearch(arr, l, mid - 1, data, compare);
    return BinarySearch(arr, mid + 1, r, data, compare);
}

static int CompareEntryName(const ImGuiPerflogEntry& val, const void* data)
{
    return strcmp(val.TestName, (const char*)data);
}

static bool Date(const char* label, char* date, int date_len, bool valid)
{
    ImGui::SetNextItemWidth(80.0f);
    bool date_valid = IsDateValid(date) && valid/*strcmp(_FilterDateFrom, _FilterDateTo) <= 0*/;
    if (!date_valid)
    {
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 0, 0, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1);
    }
    bool date_changed = ImGui::InputText(label, date, date_len);
    if (!date_valid)
    {
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }
    return date_changed;
}

struct GetPlotPointData
{
    ImGuiPerfLog* Perf;
    double Shift;
    int BatchIndex;
};

static ImPlotPoint GetPlotPoint(void* data, int idx)
{
    GetPlotPointData* d = (GetPlotPointData*)data;
    ImGuiPerfLog* perf = d->Perf;
    ImGuiPerflogEntry* entry = perf->GetEntryByBatchIdx(d->BatchIndex, perf->_VisibleLabelPointers[idx]);
    ImPlotPoint pt;
    pt.x = entry ? entry->DtDeltaMs : 0.0;
    pt.y = idx + d->Shift;
    return pt;
}

static void RenderFilterInput(ImGuiPerfLog* perf, const char* hint)
{
    if (ImGui::IsWindowAppearing())
    {
        perf->_FilterInputWidth = -1;
        perf->_Filter.clear();
    }
    ImGui::SetNextItemWidth(perf->_FilterInputWidth);
    ImGui::InputTextWithHint("##filter", hint, &perf->_Filter);
    if (perf->_FilterInputWidth < 1.0f)
        perf->_FilterInputWidth = ImGui::GetItemRectSize().x;
}

static bool RenderMultiSelectFilter(ImGuiPerfLog* perf, const char* filter_hint, ImVector<ImGuiPerflogEntry*>* entries, HashEntryFn hash, FormatEntryLabelFn format)
{
    ImGuiContext& g = *GImGui;
    Str256f buf("");
    ImGuiStorage& visibility = perf->_Settings.Visibility;
    bool modified = false;
    RenderFilterInput(perf, filter_hint);

    // Keep popup open for multiple actions if SHIFT is pressed.
    if (g.IO.KeyShift)
        ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);

    if (ImGui::MenuItem("Show All"))
    {
        for (ImGuiPerflogEntry* entry : *entries)
        {
            buf.clear();
            format(perf, &buf, entry);
            if (strstr(buf.c_str(), perf->_Filter.c_str()) != NULL)
                visibility.SetBool(hash(entry), true);
        }
    }

    if (ImGui::MenuItem("Hide All"))
    {
        for (ImGuiPerflogEntry* entry : *entries)
        {
            buf.clear();
            format(perf, &buf, entry);
            if (strstr(buf.c_str(), perf->_Filter.c_str()) != NULL)
                visibility.SetBool(hash(entry), false);
        }
    }

    for (ImGuiPerflogEntry* entry : *entries)
    {
        buf.clear();
        format(perf, &buf, entry);
        if (strstr(buf.c_str(), perf->_Filter.c_str()) == NULL)   // Filter out entries not matching a filter query
            continue;

        ImGuiID build_id = hash(entry);
        bool visible = visibility.GetBool(build_id, true);
        if (ImGui::MenuItem(buf.c_str(), NULL, &visible))
        {
            modified = true;
            if (g.IO.KeyCtrl)
            {
                for (ImGuiPerflogEntry* entry2 : *entries)
                {
                    ImGuiID build_id2 = hash(entry2);
                    visibility.SetBool(build_id2, !visibility.GetBool(build_id2, true));
                }
            }
            else
            {
                visibility.SetBool(build_id, !visibility.GetBool(build_id, true));
            }
        }
    }

    if (g.IO.KeyShift)
        ImGui::PopItemFlag();

    return modified;
}

static bool RenderSingleSelectFilter(ImGuiPerfLog* perf, const char* filter_hint, ImVector<ImGuiPerflogEntry*>* entries, int* index, FormatEntryLabelFn format)
{
    Str256f buf("");
    int prev_index = *index;
    RenderFilterInput(perf, filter_hint);
    for (ImGuiPerflogEntry*& entry : *entries)
    {
        buf.clear();
        format(perf, &buf, entry);
        if (strstr(buf.c_str(), perf->_Filter.c_str()) == NULL)   // Filter out entries not matching a filter query
            continue;

        int current_index = entries->index_from_ptr(&entry);
        bool is_selected = current_index == *index;
        if (ImGui::MenuItem(buf.c_str(), NULL, &is_selected) && is_selected)
            *index = current_index;
    }

    return prev_index != *index;
}

static void PerflogSettingsHandler_ClearAll(ImGuiContext*, ImGuiSettingsHandler* ini_handler)
{
    ImGuiPerfLog* perflog = (ImGuiPerfLog*)ini_handler->UserData;
    perflog->_Settings.Clear();
}

static void* PerflogSettingsHandler_ReadOpen(ImGuiContext*, ImGuiSettingsHandler*, const char*)
{
    return (void*)1;
}

static void PerflogSettingsHandler_ReadLine(ImGuiContext*, ImGuiSettingsHandler* ini_handler, void*, const char* line)
{
    ImGuiPerfLog* perflog = (ImGuiPerfLog*)ini_handler->UserData;
    char buf[128];
    ImU64 timestamp;
    int visible, combine;
         if (sscanf(line, "DateFrom=%10s", perflog->_FilterDateFrom))               { }
    else if (sscanf(line, "DateTo=%10s", perflog->_FilterDateTo))                   { }
    else if (sscanf(line, "CombineByBuildInfo=%d", &combine))                       { perflog->_CombineByBuildInfo = !!combine; }
    else if (sscanf(line, "Baseline=%llu", &perflog->_Settings.BaselineTimestamp))  { }
    else if (sscanf(line, "TestVisibility=%[^=]=%d", buf, &visible))                { perflog->_Settings.Visibility.SetBool(ImHashStr(buf), !!visible); }
    else if (sscanf(line, "BuildVisibility=%llu=%d", &timestamp, &visible))         { perflog->_Settings.Visibility.SetBool(ImHashData(&timestamp, sizeof(timestamp)), !!visible); }
}

static void PerflogSettingsHandler_ApplyAll(ImGuiContext*, ImGuiSettingsHandler* ini_handler)
{
    ImGuiPerfLog* perflog = (ImGuiPerfLog*)ini_handler->UserData;
    perflog->_Dirty = true;
    perflog->_BaselineBatchIndex = -1;  // Index will be re-discovered from _Settings.BaselineTimestamp in _Rebuild().
}

static void PerflogSettingsHandler_WriteAll(ImGuiContext*, ImGuiSettingsHandler* ini_handler, ImGuiTextBuffer* buf)
{
    ImGuiPerfLog* perflog = (ImGuiPerfLog*)ini_handler->UserData;
    if (perflog->_Data.empty())
        return;
    buf->appendf("[%s][Data]\n", ini_handler->TypeName);
    buf->appendf("DateFrom=%s\n", perflog->_FilterDateFrom);
    buf->appendf("DateTo=%s\n", perflog->_FilterDateTo);
    buf->appendf("CombineByBuildInfo=%d\n", perflog->_CombineByBuildInfo);
    if (perflog->_BaselineBatchIndex >= 0)
        buf->appendf("Baseline=%llu\n", perflog->_Legend[perflog->_BaselineBatchIndex]->Timestamp);
    for (ImGuiPerflogEntry* entry : perflog->_Labels)
        buf->appendf("TestVisibility=%s=%d\n", entry->TestName, perflog->_Settings.Visibility.GetBool(PerflogHashTestName(entry), true));
    for (ImGuiPerflogEntry* entry : perflog->_Legend)
        buf->appendf("BuildVisibility=%llu=%d\n", entry->Timestamp, perflog->_Settings.Visibility.GetBool(PerflogHashBuildInfo(entry), true));
    buf->append("\n");
}

ImGuiPerfLog::ImGuiPerfLog()
{
    ImGuiContext& g = *GImGui;
    ImGuiSettingsHandler ini_handler;
    ini_handler.TypeName = "Perflog";
    ini_handler.TypeHash = ImHashStr("Perflog");
    ini_handler.ClearAllFn = PerflogSettingsHandler_ClearAll;
    ini_handler.ReadOpenFn = PerflogSettingsHandler_ReadOpen;
    ini_handler.ReadLineFn = PerflogSettingsHandler_ReadLine;
    ini_handler.ApplyAllFn = PerflogSettingsHandler_ApplyAll;
    ini_handler.WriteAllFn = PerflogSettingsHandler_WriteAll;
    ini_handler.UserData = this;
    g.SettingsHandlers.push_back(ini_handler);
}

bool ImGuiPerfLog::Load(const char* file_name)
{
    if (!_CSVParser.Load(file_name))
        return false;

    if (_CSVParser.Columns != 11)
    {
        IM_ASSERT(0 && "Invalid number of columns.");
        return false;
    }

    ImStrncpy(_FilterDateFrom, "9999-99-99", IM_ARRAYSIZE(_FilterDateFrom));
    ImStrncpy(_FilterDateTo, "0000-00-00", IM_ARRAYSIZE(_FilterDateFrom));

    // Read perf test entries from CSV
    for (int row = 0; row < _CSVParser.Rows; row++)
    {
        ImGuiPerflogEntry entry;
        int col = 0;
        sscanf(_CSVParser.GetCell(row, col++), "%llu", &entry.Timestamp);
        entry.Category = _CSVParser.GetCell(row, col++);
        entry.TestName = _CSVParser.GetCell(row, col++);
        sscanf(_CSVParser.GetCell(row, col++), "%lf", &entry.DtDeltaMs);
        sscanf(_CSVParser.GetCell(row, col++), "x%d", &entry.PerfStressAmount);
        entry.GitBranchName = _CSVParser.GetCell(row, col++);
        entry.BuildType = _CSVParser.GetCell(row, col++);
        entry.Cpu = _CSVParser.GetCell(row, col++);
        entry.OS = _CSVParser.GetCell(row, col++);
        entry.Compiler = _CSVParser.GetCell(row, col++);
        entry.Date = _CSVParser.GetCell(row, col++);
        _CSVData.push_back(entry);

        if (strcmp(_FilterDateFrom, entry.Date) > 0)
            ImStrncpy(_FilterDateFrom, entry.Date, IM_ARRAYSIZE(_FilterDateFrom));
        if (strcmp(_FilterDateTo, entry.Date) < 0)
            ImStrncpy(_FilterDateTo, entry.Date, IM_ARRAYSIZE(_FilterDateTo));
    }

    _Labels.resize(0);
    _Legend.resize(0);

    return true;
}

bool ImGuiPerfLog::Save(const char* file_name)
{
    // Log to .csv
    FILE* f = fopen(file_name, "w+t");
    if (f == NULL)
        return false;

    for (ImGuiPerflogEntry& entry : _CSVData)
        fprintf(f, "%llu,%s,%s,%.3f,x%d,%s,%s,%s,%s,%s,%s\n", entry.Timestamp, entry.Category, entry.TestName,
            entry.DtDeltaMs, entry.PerfStressAmount, entry.GitBranchName, entry.BuildType, entry.Cpu, entry.OS,
            entry.Compiler, entry.Date);
    fflush(f);
    fclose(f);
    return true;
}

void ImGuiPerfLog::AddEntry(ImGuiPerflogEntry* entry)
{
    _CSVData.push_back(*entry);
    _Dirty = true;
}

void ImGuiPerfLog::_Rebuild()
{
    if (_CSVData.empty())
        return;

    _Dirty = false;
    _Data.clear();

    // FIXME: What if entries have a varying timestep?
    if (_CombineByBuildInfo)
    {
        // Combine similar runs by build config.
        ImGuiStorage index;
        ImVector<int> counts;
        for (ImGuiPerflogEntry& entry : _CSVData)
        {
            if (strcmp(entry.Date, _FilterDateFrom) < 0)
                continue;
            if (strcmp(entry.Date, _FilterDateTo) > 0)
                continue;

            ImGuiID build_id = ImHashStr(entry.BuildType);
            build_id = ImHashStr(entry.OS, 0, build_id);
            build_id = ImHashStr(entry.Cpu, 0, build_id);
            build_id = ImHashStr(entry.Compiler, 0, build_id);
            build_id = ImHashStr(entry.GitBranchName, 0, build_id);
            build_id = ImHashStr(entry.TestName, 0, build_id);

            int i = index.GetInt(build_id, -1);
            if (i < 0)
            {
                _Data.push_back(entry);
                _Data.back().DtDeltaMs = 0.0;
                counts.push_back(0);
                i = _Data.Size - 1;
                index.SetInt(build_id, i);
                IM_ASSERT(_Data.Size == counts.Size);
            }

            ImGuiPerflogEntry& new_entry = _Data[i];
            new_entry.Date = "";
            new_entry.DtDeltaMs += entry.DtDeltaMs;
            new_entry.DtDeltaMsMin = ImMin(new_entry.DtDeltaMsMin, entry.DtDeltaMs);
            new_entry.DtDeltaMsMax = ImMax(new_entry.DtDeltaMsMax, entry.DtDeltaMs);
            counts[i]++;
        }

        // Average data
        for (int i = 0; i < _Data.Size; i++)
        {
            _Data[i].NumSamples = counts[i];
            _Data[i].DtDeltaMs /= counts[i];
        }
    }
    else
    {
        // Copy to a new buffer that we are going to modify.
        for (ImGuiPerflogEntry& entry : _CSVData)
        {
            if (strcmp(entry.Date, _FilterDateFrom) < 0)
                continue;
            if (strcmp(entry.Date, _FilterDateTo) > 0)
                continue;
            _Data.push_back(entry);
        }
    }

    if (_Data.empty())
        return;

    // Sort entries by timestamp and test name, so all tests are grouped by batch and have a consistent entry order.
    ImQsort(_Data.Data, _Data.Size, sizeof(ImGuiPerflogEntry), &PerflogComparerByTimestampAndTestName);

    // Index data for a convenient access.
    ImGuiStorage label_index;
    _Legend.resize(0);
    _Labels.resize(0);
    ImU64 last_timestamp = UINT64_MAX;
    int batch_size = 0;
    for (int i = 0; i < _Data.Size; i++)
    {
        ImGuiPerflogEntry* entry = &_Data[i];
        if (entry->Timestamp != last_timestamp)
        {
            _Legend.push_back(entry);
            last_timestamp = entry->Timestamp;
            batch_size = 0;
        }
        batch_size++;
        ImGuiID name_id = ImHashStr(entry->TestName);
        if (label_index.GetInt(name_id, -1) < 0)
        {
            label_index.SetInt(name_id, _Labels.Size);
            _Labels.push_back(entry);
        }
    }
    _SelectedTest = ImClamp(_SelectedTest, 0, _Labels.Size - 1);
    _BaselineBatchIndex = ImClamp(_SelectedTest, 0, _Legend.Size - 1);

    _CalculateLegendAlignment();
}

void ImGuiPerfLog::Clear()
{
    _CSVData.clear();
    _Data.clear();
    _Labels.clear();
    _Legend.clear();
    _Settings.Clear();
}

void ImGuiPerfLog::ShowUI(ImGuiTestEngine* engine)
{
#ifdef IMGUI_TEST_ENGINE_ENABLE_IMPLOT
    ImGuiContext& g = *GImGui;
    Str256f label("");

    if (_Dirty)
        _Rebuild();

    // -----------------------------------------------------------------------------------------------------------------
    // Render utility buttons
    // -----------------------------------------------------------------------------------------------------------------
    // Back to perf test list.
    if (ImGui::Button("<< Back"))
        engine->UiShowPerflog = false;
    ImGui::SameLine();

    // Date filter
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Filter by date:");
    ImGui::SameLine();

    bool date_changed = Date("##date-from", _FilterDateFrom, IM_ARRAYSIZE(_FilterDateFrom), strcmp(_FilterDateFrom, _FilterDateTo) <= 0);
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("Date From Menu");
    ImGui::SameLine();
    ImGui::TextUnformatted("-");
    ImGui::SameLine();
    date_changed |= Date("##date-to", _FilterDateTo, IM_ARRAYSIZE(_FilterDateTo), strcmp(_FilterDateFrom, _FilterDateTo) <= 0);
    _Dirty = date_changed && IsDateValid(_FilterDateFrom) && IsDateValid(_FilterDateTo) && strcmp(_FilterDateFrom, _FilterDateTo) <= 0;
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("Date To Menu");
    ImGui::SameLine();

    if (ImGui::BeginPopup("Date From Menu"))
    {
        if (ImGui::MenuItem("Set Min"))
        {
            for (ImGuiPerflogEntry& entry : _CSVData)
                if (strcmp(_FilterDateFrom, entry.Date) > 0)
                {
                    ImStrncpy(_FilterDateFrom, entry.Date, IM_ARRAYSIZE(_FilterDateFrom));
                    _Dirty = true;
                }
        }
        if (ImGui::MenuItem("Set Today"))
        {
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            ImFormatString(_FilterDateFrom, IM_ARRAYSIZE(_FilterDateFrom), "%d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
            _Dirty = true;
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Date To Menu"))
    {
        if (ImGui::MenuItem("Set Max"))
        {
            for (ImGuiPerflogEntry& entry : _CSVData)
                if (strcmp(_FilterDateTo, entry.Date) < 0)
                {
                    ImStrncpy(_FilterDateTo, entry.Date, IM_ARRAYSIZE(_FilterDateTo));
                    _Dirty = true;
                }
        }
        if (ImGui::MenuItem("Set Today"))
        {
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            ImFormatString(_FilterDateTo, IM_ARRAYSIZE(_FilterDateTo), "%d-%02d-%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
            _Dirty = true;
        }
        ImGui::EndPopup();
    }

    // Total visible builds.
    int num_visible_builds = 0;
    for (ImGuiPerflogEntry* entry : _Legend)
        if (_IsVisibleBuild(entry))
            num_visible_builds++;

    // Gather pointers of visible labels. ImPloit requires such array for rendering.
    _VisibleLabelPointers.resize(0);
    for (ImGuiPerflogEntry* entry : _Labels)
        if (_IsVisibleTest(entry))
            _VisibleLabelPointers.push_back(entry->TestName);

    if (ImGui::Button(Str16f("Filter builds (%d/%d)", num_visible_builds, _Legend.Size).c_str()))
        ImGui::OpenPopup("Filter builds");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide or show individual builds.");
    ImGui::SameLine();
    if (ImGui::Button(Str16f("Filter tests (%d/%d)", _VisibleLabelPointers.Size, _Labels.Size).c_str()))
        ImGui::OpenPopup("Filter perfs");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide or show individual tests.");
    ImGui::SameLine();
    _Dirty |= ImGui::Checkbox("Combine by build info", &_CombineByBuildInfo);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Combine multiple runs with same build info into one averaged build entry.");
    ImGui::SameLine();

    if (ImGui::Button("Clear All"))
        ImGui::OpenPopup("Clear All");
    ImGui::SameLine();

    if (ImGui::Button(Str128f("Info Table: %s", _Labels.empty() ? "???" : _Labels[_SelectedTest]->TestName).c_str()))
        ImGui::OpenPopup("Select Test");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Select a test for display of additional information in table below the plot.");
    ImGui::SameLine();

    // Align help button to the right.
    float help_pos = ImGui::GetWindowContentRegionMax().x - g.Style.FramePadding.x * 2 - ImGui::CalcTextSize("Help").x;
    if (help_pos > ImGui::GetCursorPosX())
        ImGui::SetCursorPosX(help_pos);
    if (ImGui::Button("Help"))
        ImGui::OpenPopup("Help");

    if (ImGui::BeginPopup("Clear All"))
    {
        ImGui::TextUnformatted("Permanently remove all perflog data?");
        ImGui::TextUnformatted("This action _CAN NOT_ be undone!");
        if (ImGui::Button("Yes, I am sure"))
        {
            ImGui::CloseCurrentPopup();
            Clear();
        }
        ImGui::EndPopup();

        if (_CSVData.empty())
            return;
    }

    if (ImGui::BeginPopup("Select Test"))
    {
        RenderSingleSelectFilter(this, "Filter Tests", &_Labels, &_SelectedTest, PerflogFormatTestName);
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Help"))
    {
        ImGui::TextUnformatted("Plot displays perf test delta time of each build (bars) per perf test (left).");
        ImGui::TextUnformatted("Extra information is displayed when hovering bars of a particular perf test and holding SHIFT.");
        ImGui::TextUnformatted("This information tooltip displays performance change compared to baseline build.");
        ImGui::TextUnformatted("To change baseline build, double-click desired build in the legend.");
        ImGui::TextUnformatted("Data of different runs may be combined together based on build information and averaged.");
        ImGui::TextUnformatted("Extra information tooltip will display min/max delta values as well as number of samples.");
        ImGui::TextUnformatted("Data may be filtered by enabling or disabling individual builds or perf tests.");
        ImGui::TextUnformatted("Hold CTRL when toggling build info or perf test visibility in order to invert visibility or other items.");
        ImGui::TextUnformatted("Hold SHIFT when toggling build info or perf test visibility in order to keep popup open.");
        ImGui::TextUnformatted("Double-click plot to fit plot into available area.");
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Filter builds"))
    {
        if (RenderMultiSelectFilter(this, "Filter by build info", &_Legend, PerflogHashBuildInfo, PerflogFormatBuildInfo))
            _CalculateLegendAlignment();
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopup("Filter perfs"))
    {
        RenderMultiSelectFilter(this, "Filter by perf test", &_Labels, PerflogHashTestName, PerflogFormatTestName);
        ImGui::EndPopup();
    }

    // Rendering a plot of empty dataset is not possible.
    if (_Data.empty() || _VisibleLabelPointers.empty() || num_visible_builds == 0)
    {
        ImGui::TextUnformatted("No data is available. Run some perf tests or adjust filter settings.");
        return;
    }

    // Splitter between two following child windows is rendered first.
    float plot_height;
    ImGui::Splitter("splitter", &plot_height, &_InfoTableHeight, ImGuiAxis_Y, +1);

    // -----------------------------------------------------------------------------------------------------------------
    // Render a plot
    // -----------------------------------------------------------------------------------------------------------------
    if (ImGui::BeginChild(ImGui::GetID("plot"), ImVec2(0, plot_height)))
    {
        // A workaround for ImPlot requiring at least two labels.
        if (_VisibleLabelPointers.Size < 2)
            _VisibleLabelPointers.push_back("");

        ImPlot::SetNextPlotTicksY(0, _VisibleLabelPointers.Size - 1, _VisibleLabelPointers.Size, _VisibleLabelPointers.Data);
        if (ImPlot::GetCurrentContext()->Plots.GetByKey(ImGui::GetID("Perflog")) == NULL)
            ImPlot::FitNextPlotAxes();   // Fit plot when appearing.
        if (!ImPlot::BeginPlot("Perflog", "Delta milliseconds", "Test", ImVec2(-1, -1)))
            return;

        const float occupy_h = 0.8f;
        const float h = occupy_h / num_visible_builds;

        // Plot bars
        int bar_index = 0;
        for (ImGuiPerflogEntry*& entry : _Legend)
        {
            int batch_index = _Legend.index_from_ptr(&entry);
            if (!_IsVisibleBuild(entry))
                continue;

            // Plot bars.
            label.clear();
            PerflogFormatBuildInfo(this, &label, entry);
            label.appendf("%s###%08X", _BaselineBatchIndex == batch_index ? " *" : "", entry->Timestamp);
            GetPlotPointData data;
            data.Shift = -h * bar_index + (float) (num_visible_builds - 1) * 0.5f * h;
            data.Perf = this;
            data.BatchIndex = batch_index;
            ImPlot::PlotBarsHG(label.c_str(), &GetPlotPoint, &data, _VisibleLabelPointers.Size, h);

            // Set baseline.
            if (ImPlot::IsLegendEntryHovered(label.c_str()) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                _BaselineBatchIndex = batch_index;

            bar_index++;
        }
        ImPlot::EndPlot();
    }
    ImGui::EndChild();

    // -----------------------------------------------------------------------------------------------------------------
    // Render perf test info table
    // -----------------------------------------------------------------------------------------------------------------
    if (ImGui::BeginChild(ImGui::GetID("info-table"), ImVec2(0, _InfoTableHeight)))
    {

        static const char* columns_combined[] = { "Branch", "Compiler", "OS", "CPU", "Build", "Stress", "Avg ms", "Min ms", "Max ms", "Num samples", "VS Baseline" };
        static const char* columns_separate[] = { "Branch", "Compiler", "OS", "CPU", "Build", "Stress", "Avg ms", "VS Baseline" };
        const char** columns = _CombineByBuildInfo ? columns_combined : columns_separate;
        int columns_num = _CombineByBuildInfo ? IM_ARRAYSIZE(columns_combined) : IM_ARRAYSIZE(columns_separate);
        ImGuiPerflogEntry* baseline_entry = NULL;
        if (_BaselineBatchIndex >= 0)
            baseline_entry = GetEntryByBatchIdx(_BaselineBatchIndex, _Labels[_SelectedTest]->TestName);
        if (!ImGui::BeginTable("PerfInfo", columns_num, ImGuiTableFlags_Borders))
            return;

        for (int i = 0; i < columns_num; i++)
            ImGui::TableSetupColumn(columns[i], ImGuiTableColumnFlags_WidthAuto);
        ImGui::TableHeadersRow();

        for (int batch_index = 0; batch_index < _Legend.Size; batch_index++)
        {
            ImGuiPerflogEntry* entry = GetEntryByBatchIdx(batch_index, _Labels[_SelectedTest]->TestName);
            if (entry == NULL || !_IsVisibleBuild(entry))
                continue;

            int column = 0;
            ImGui::TableNextRow();

            // Build info
            ImGui::TableSetColumnIndex(column++);
            ImGui::TextUnformatted(entry->GitBranchName);
            ImGui::TableSetColumnIndex(column++);
            ImGui::TextUnformatted(entry->Compiler);
            ImGui::TableSetColumnIndex(column++);
            ImGui::TextUnformatted(entry->OS);
            ImGui::TableSetColumnIndex(column++);
            ImGui::TextUnformatted(entry->Cpu);
            ImGui::TableSetColumnIndex(column++);
            ImGui::TextUnformatted(entry->BuildType);
            ImGui::TableSetColumnIndex(column++);
            ImGui::Text("x%d", entry->PerfStressAmount);

            // Avg ms
            ImGui::TableSetColumnIndex(column++);
            ImGui::Text("%.3lfms", entry->DtDeltaMs);

            if (_CombineByBuildInfo)
            {
                // Min ms
                ImGui::TableSetColumnIndex(column++);
                ImGui::Text("%.3lfms", entry->DtDeltaMsMin);

                // Max ms
                ImGui::TableSetColumnIndex(column++);
                ImGui::Text("%.3lfms", entry->DtDeltaMsMax);

                // Num samples
                ImGui::TableSetColumnIndex(column++);
                ImGui::Text("%d", entry->NumSamples);
            }

            // VS Baseline
            ImGui::TableSetColumnIndex(column++);
            if (batch_index == _BaselineBatchIndex)
            {
                ImGui::TextUnformatted("baseline");
            }
            else if (baseline_entry == NULL)
            {
                ImGui::TextUnformatted("--");
            }
            else if (batch_index != _BaselineBatchIndex)
            {
                double percent_vs_first = 100.0 / baseline_entry->DtDeltaMs * entry->DtDeltaMs;
                double dt_change = -(100.0 - percent_vs_first);
                if (ImAbs(dt_change) > 0.001f)
                    ImGui::Text("%+.2lf%% (%s)", dt_change, dt_change < 0.0f ? "faster" : "slower");
                else
                    ImGui::TextUnformatted("==");
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
#else
    IM_ASSERT(0 && "Perflog UI is not enabled, because ImPlot is not available (IMGUI_TEST_ENGINE_ENABLE_IMPLOT is not defined).");
#endif
}

void ImGuiPerfLog::ViewOnly(const char** perf_names)
{
    // Data would not be built if we tried to view perflog of a particular test without first opening perflog via button. We need data to be built to hide perf tests.
    if (_Data.empty())
        _Rebuild();

    // Hide other perf tests.
    for (ImGuiPerflogEntry* entry : _Labels)
    {
        bool visible = false;
        for (const char** p_name = perf_names; !visible && *p_name; p_name++)
            visible |= strcmp(entry->TestName, *p_name) == 0;
        _Settings.Visibility.SetBool(PerflogHashTestName(entry), visible);
    }
}

void ImGuiPerfLog::ViewOnly(const char* perf_name)
{
    const char* names[] = { perf_name, NULL };
    ViewOnly(names);
}

ImGuiPerflogEntry* ImGuiPerfLog::GetEntryByBatchIdx(int idx, const char* perf_name)
{
    IM_ASSERT(idx < _Legend.Size);
    ImGuiPerflogEntry* first = _Legend[idx];
    if (perf_name == NULL)
        return first;

    // Find a specific perf test.
    int batch_size = 0;
    for (ImGuiPerflogEntry* entry = first; entry->Timestamp == first->Timestamp; entry++, batch_size++);
    int test_idx = BinarySearch(first, 0, batch_size - 1, perf_name, CompareEntryName);
    if (test_idx < 0)
        return nullptr;
    return &first[test_idx];
}

bool ImGuiPerfLog::_IsVisibleBuild(ImGuiPerflogEntry* entry)
{
    return _Settings.Visibility.GetBool(PerflogHashBuildInfo(entry), true);
}

bool ImGuiPerfLog::_IsVisibleTest(ImGuiPerflogEntry* entry)
{
    return _Settings.Visibility.GetBool(PerflogHashTestName(entry), true);
}

void ImGuiPerfLog::_CalculateLegendAlignment()
{
    // Estimate paddings for legend format so it looks nice and aligned.
    _AlignStress = _AlignType = _AlignOs = _AlignCompiler = _AlignBranch = 0;
    for (ImGuiPerflogEntry* entry : _Legend)
    {
        if (!_Settings.Visibility.GetBool(PerflogHashBuildInfo(entry), true))
            continue;
        _AlignStress = ImMax(_AlignStress, (int)ceil(log10(entry->PerfStressAmount)));
        _AlignType = ImMax(_AlignType, (int)strlen(entry->BuildType));
        _AlignOs = ImMax(_AlignOs, (int)strlen(entry->OS));
        _AlignCompiler = ImMax(_AlignCompiler, (int)strlen(entry->Compiler));
        _AlignBranch = ImMax(_AlignBranch, (int)strlen(entry->GitBranchName));
    }
}
