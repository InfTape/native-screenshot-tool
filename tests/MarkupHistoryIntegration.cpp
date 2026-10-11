#include <Windows.h>
#include <Psapi.h>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "capture/CapturedImage.h"
#include "editing/ImageMarkupService.h"
#include "ui/SelectionSession.h"

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

capture::CapturedImage MakeImage(int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[offset] = static_cast<std::uint8_t>((x + y * 3) % 256);
            pixels[offset + 1] = static_cast<std::uint8_t>((x * 5 + y) % 256);
            pixels[offset + 2] = static_cast<std::uint8_t>((x * 3 + y * 7) % 256);
            // Include transparent pixels, as in a captured window's corners.
            pixels[offset + 3] = static_cast<std::uint8_t>((x + y) % 256);
        }
    }
    return capture::CapturedImage(width, height, std::move(pixels));
}

editing::MarkupCommand MakeCommand(int index, int width, int height) {
    editing::MarkupCommand command;
    command.clip_bounds = RECT{7, 6, width - 9, height - 8};
    command.color = RGB(20 + index * 7 % 230, 200 - index * 3 % 190, 50 + index * 5 % 200);
    command.thickness = 2 + index % 7;
    command.block_size = 4 + index % 11;
    command.rect = RECT{-3 + index % 12, 2, width * 3 / 4, height * 3 / 4};
    command.start = POINT{-5, height / 3};
    command.end = POINT{width + 4, height * 2 / 3};
    command.points = {POINT{-4, height / 2}, POINT{width / 3, height / 3},
                      POINT{width / 3, height / 3}, POINT{width / 2, height * 3 / 4},
                      POINT{width + 5, height / 2}};
    constexpr editing::MarkupTool tools[] = {
        editing::MarkupTool::Rectangle, editing::MarkupTool::Brush,
        editing::MarkupTool::Mosaic, editing::MarkupTool::Arrow,
        editing::MarkupTool::Mosaic};
    command.tool = tools[index % 5];
    return command;
}

bool SameImage(const capture::CapturedImage& actual, const capture::CapturedImage& expected) {
    return actual.Width() == expected.Width() && actual.Height() == expected.Height() &&
           actual.Pixels() == expected.Pixels();
}

void CheckHistory() {
    const auto original = MakeImage(257, 131);
    const auto original_pixels = original.Pixels();
    auto image = original;
    auto expected = original;
    std::vector<capture::CapturedImage> snapshots{original};
    ui::SelectionSession session;
    for (int index = 0; index < 25; ++index) {
        auto command = MakeCommand(index, image.Width(), image.Height());
        Require(static_cast<bool>(editing::ImageMarkupService::ApplyCommand(expected, command)),
                "Reference edit failed");
        Require(static_cast<bool>(session.ApplyEdit(image, command, 20)), "History edit failed");
        Require(SameImage(image, expected), "Edit changed rendering compared with direct drawing");
        snapshots.push_back(expected);
        // History must own the stroke data, rather than refer to input buffers.
        command.points.clear();
        command.rect = {};
    }
    Require(original.Pixels() == original_pixels, "Editing mutated the captured source");

    editing::MarkupCommand invalid;
    Require(!session.ApplyEdit(image, invalid, 20), "Unsupported command accepted");
    auto invalid_arrow = MakeCommand(3, image.Width(), image.Height());
    invalid_arrow.end = invalid_arrow.start;
    Require(!session.ApplyEdit(image, invalid_arrow, 20), "Zero-length arrow accepted");
    Require(!session.ApplyEdit(image, MakeCommand(0, 257, 131), 0), "Zero history limit accepted");
    Require(SameImage(image, snapshots.back()), "Failed edit mutated the image");

    // Includes checkpoints containing mosaics over previous annotations.
    for (int index = 24; index >= 5; --index) {
        Require(session.CanUndo(), "History lost an undo step");
        Require(static_cast<bool>(session.UndoLastEdit(image)), "Undo replay failed");
        Require(SameImage(image, snapshots[index]), "Undo pixels differ from the original state");
    }
    Require(!session.CanUndo(), "History exceeded the 20-step limit");
    Require(static_cast<bool>(session.UndoLastEdit(image)), "Empty undo failed");
    Require(SameImage(image, snapshots[5]), "Empty undo changed the checkpoint");

    // Editing after an undo must discard the removed operation permanently.
    auto first = MakeCommand(1, 257, 131);
    auto removed = MakeCommand(3, 257, 131);
    auto replacement = MakeCommand(2, 257, 131);
    Require(static_cast<bool>(session.ApplyEdit(image, first, 20)), "Branch first edit failed");
    auto branch_base = image;
    Require(static_cast<bool>(session.ApplyEdit(image, removed, 20)), "Branch second edit failed");
    Require(static_cast<bool>(session.UndoLastEdit(image)), "Branch undo failed");
    Require(SameImage(image, branch_base), "Branch undo retained the removed annotation");
    Require(static_cast<bool>(session.ApplyEdit(image, replacement, 20)), "Replacement edit failed");
    Require(static_cast<bool>(session.UndoLastEdit(image)), "Replacement undo failed");
    Require(SameImage(image, branch_base), "Replacement undo revived a discarded annotation");
    Require(static_cast<bool>(session.UndoLastEdit(image)), "Branch final undo failed");
    Require(SameImage(image, snapshots[5]), "Branch lost the checkpoint");

    session.Reset(false);
    image = MakeImage(91, 63);
    const auto next_capture = image;
    auto dot = MakeCommand(1, 91, 63);
    dot.points = {POINT{40, 30}};
    Require(static_cast<bool>(session.ApplyEdit(image, dot, 1)), "Single-point brush failed");
    Require(static_cast<bool>(session.UndoLastEdit(image)), "New session undo failed");
    Require(SameImage(image, next_capture), "New capture reused an old checkpoint");
    session.ClearHistory();
    Require(!session.CanUndo(), "Cleanup retained history");
    std::cout << "Pixel equivalence: all tools, overlaps, clipping, alpha, checkpoints, branches and reset passed\n";
}

SIZE_T PrivateBytes() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    Require(GetProcessMemoryInfo(GetCurrentProcess(),
        reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters)) != FALSE,
        "Memory query failed");
    return counters.PrivateUsage;
}

SIZE_T PrivateWorkingSet() {
    std::vector<ULONG_PTR> storage(4096);
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (QueryWorkingSet(GetCurrentProcess(), storage.data(),
                            static_cast<DWORD>(storage.size() * sizeof(ULONG_PTR)))) {
            const auto* info = reinterpret_cast<const PSAPI_WORKING_SET_INFORMATION*>(storage.data());
            SIZE_T pages = 0;
            for (ULONG_PTR index = 0; index < info->NumberOfEntries; ++index) {
                if (!info->WorkingSetInfo[index].Shared) ++pages;
            }
            SYSTEM_INFO system{};
            GetSystemInfo(&system);
            return pages * system.dwPageSize;
        }
        Require(GetLastError() == ERROR_BAD_LENGTH, "Working set query failed");
        storage.resize(storage.size() * 2);
    }
    throw std::runtime_error("Working set query exceeded its size limit");
}

void Benchmark(bool old_history) {
    // Run the old and new modes in separate processes to avoid heap/cache carryover.
    const SIZE_T baseline = PrivateBytes();
    const SIZE_T baseline_working_set = PrivateWorkingSet();
    const auto started = std::chrono::steady_clock::now();
    const auto original = MakeImage(2880, 1800);
    auto image = original;
    ui::SelectionSession session;
    std::vector<capture::CapturedImage> old_snapshots;
    for (int index = 0; index < 20; ++index) {
        auto command = MakeCommand(index, image.Width(), image.Height());
        if (old_history) {
            old_snapshots.push_back(image);
            Require(static_cast<bool>(editing::ImageMarkupService::ApplyCommand(image, command)),
                    "Old benchmark edit failed");
        } else {
            Require(static_cast<bool>(session.ApplyEdit(image, command, 20)), "Benchmark edit failed");
        }
    }
    const SIZE_T after_edits = PrivateBytes();
    const SIZE_T after_edits_working_set = PrivateWorkingSet();
    const auto undo_started = std::chrono::steady_clock::now();
    if (old_history) {
        image = std::move(old_snapshots.back());
        old_snapshots.pop_back();
    } else {
        Require(static_cast<bool>(session.UndoLastEdit(image)), "Large image undo failed");
    }
    const auto completed = std::chrono::steady_clock::now();
    const double mib = 1024.0 * 1024.0;
    std::cout << "mode,baseline_private_commit_mib,after_20_edits_private_commit_mib,baseline_private_ws_mib,after_20_edits_private_ws_mib,undo_ms,edit_ms\n"
              << (old_history ? "old" : "new") << ',' << baseline / mib << ',' << after_edits / mib << ','
              << baseline_working_set / mib << ',' << after_edits_working_set / mib << ','
              << std::chrono::duration<double, std::milli>(completed - undo_started).count() << ','
              << std::chrono::duration<double, std::milli>(undo_started - started).count() << '\n';
    if (!old_history) {
        // Leave room for D2D/driver caches, but catch a return to full-frame snapshots.
        Require(after_edits - baseline < 8 * original.Pixels().size(),
                "Twenty edits retained too many full-frame images");
        session.ClearHistory();
        Require(!session.CanUndo(), "Large image cleanup retained history");
    }
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark-old") Benchmark(true);
        else if (argc == 2 && std::string_view(argv[1]) == "--benchmark") Benchmark(false);
        else {
            CheckHistory();
            Benchmark(false);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
