#include "gui.h"
#include <gtkmm.h>
#include <iostream>
#include <thread>
#include <utility>

#include "gui_common.h"
#include "xflarchive.h"

// ---- Column record for the file list ----
struct FileRecord : public Gtk::TreeModelColumnRecord {
    Gtk::TreeModelColumn<Glib::ustring> inputPath;
    Gtk::TreeModelColumn<Glib::ustring> outputPath;
    Gtk::TreeModelColumn<Glib::ustring> fileType;
    Gtk::TreeModelColumn<Glib::ustring> status;

    FileRecord() { add(inputPath); add(outputPath); add(fileType); add(status); }
};

static FileRecord g_columns;
static Glib::RefPtr<Gtk::ListStore> g_store;
static Gtk::ComboBoxText* g_encodingCombo = nullptr;
static Gtk::Entry* g_refEntry = nullptr;
static Gtk::Entry* g_outDirEntry = nullptr;
static Gtk::CheckButton* g_recursiveCheck = nullptr;
static Gtk::CheckButton* g_packOnlyCheck = nullptr;
static Gtk::CheckButton* g_unpackOnlyCheck = nullptr;
static Gtk::CheckButton* g_gscToTxtCheck = nullptr;
static Gtk::CheckButton* g_vorbisInWavCheck = nullptr;
static Gtk::SpinButton* g_workersSpin = nullptr;
static Gtk::Button* g_convertBtn = nullptr;
static Gtk::ProgressBar* g_progress = nullptr;
static Gtk::Label* g_statusLabel = nullptr;
static Gtk::Window* g_window = nullptr;

static void showDiagnostics(const std::string& report, bool hasErrors) {
    Gtk::Dialog dialog(hasErrors ? "Completed with errors" : "Completed with warnings",
                       *g_window, true);
    dialog.set_default_size(680, 400);
    Gtk::ScrolledWindow scroll;
    scroll.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
    Gtk::TextView text;
    text.set_editable(false);
    text.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
    text.get_buffer()->set_text(report);
    scroll.add(text);
    dialog.get_content_area()->pack_start(scroll);
    auto* copy = dialog.add_button("Copy details", 1);
    dialog.add_button("Close", Gtk::RESPONSE_CLOSE);
    dialog.show_all_children();
    while (dialog.run() == 1) {
        auto clipboard = Gtk::Clipboard::get();
        clipboard->set_text(report);
        clipboard->store();
        copy->set_label("Copied! Copy again");
    }
}

struct ConversionJob {
    std::string inputPath;
    std::string outputPath;
    Gtk::TreeModel::Path rowPath;
};

// ---- Add files to the list ----
static void addFiles(const std::vector<std::string>& paths, const std::string& outDir) {
    const bool gscToTsc = !g_gscToTxtCheck || !g_gscToTxtCheck->get_active();
    const std::string encoding = g_encodingCombo ?
        g_encodingCombo->get_active_id() : "CP932";
    for (const auto& p : paths) {
        if (!liarsoft::gui::isSupported(p)) continue;
        auto row = *(g_store->append());
        row[g_columns.inputPath]  = p;
        row[g_columns.outputPath] = liarsoft::gui::guessOutput(
            p, outDir, gscToTsc, encoding);
        row[g_columns.fileType]   = liarsoft::gui::guessType(
            p, gscToTsc, encoding);
        row[g_columns.status]     = "Ready";
    }
}

// ---- Conversion worker thread ----
static void convertAll(std::vector<ConversionJob> jobs,
                       const std::string& encoding, const std::string& refPath,
                       bool recursive, bool packOnly, bool unpackOnly,
                       bool gscToTsc, bool vorbisInWav, unsigned workers) {
    int total = jobs.size();
    int done = 0;
    int totalWarnings = 0;
    bool hasErrors = false;
    std::vector<liarsoft::gui::ConversionDiagnostic> diagnostics;
    const liarsoft::gui::ConversionOptions options{
        encoding, refPath, recursive, gscToTsc, unpackOnly, vorbisInWav, workers};

    for (const auto& job : jobs) {
        const std::string& in = job.inputPath;
        const std::string& out = job.outputPath;
        const auto& rowPath = job.rowPath;
        int processingNumber = done + 1;
        if (!liarsoft::matchesOperationMode(in, packOnly, unpackOnly,
                                            recursive)) {
            done++;
            Glib::signal_idle().connect_once([rowPath, done, total]() {
                auto row = g_store->get_iter(rowPath);
                if (row) (*row)[g_columns.status] = "SKIPPED";
                g_progress->set_fraction(static_cast<double>(done) / total);
                g_statusLabel->set_text(
                    Glib::ustring::format("Done ", done, " of ", total));
            });
            continue;
        }

        Glib::signal_idle().connect_once([rowPath, done, total, processingNumber]() {
            auto row = g_store->get_iter(rowPath);
            if (row) (*row)[g_columns.status] = "Processing...";
            double frac = static_cast<double>(done) / total;
            g_progress->set_fraction(frac);
            g_statusLabel->set_text(
                Glib::ustring::format("Processing ", processingNumber, " of ", total));
        });

        // Process this file
        std::vector<std::string> warnings;

        try {
            warnings = liarsoft::gui::convert(in, out, options);

            for (const auto& warning : warnings)
                std::cerr << "Warning: " << warning << std::endl;
            totalWarnings += static_cast<int>(warnings.size());
            for (const auto& warning : warnings)
                diagnostics.push_back({false, in, out, warning});
            std::string rowStatus = warnings.empty()
                ? "OK" : "WARN (" + std::to_string(warnings.size()) + ")";
            std::string detail = warnings.empty() ? "" : warnings.front();
            done++;
            Glib::signal_idle().connect_once([rowPath, done, total, rowStatus, detail]() {
                auto row = g_store->get_iter(rowPath);
                if (row) (*row)[g_columns.status] = rowStatus;
                double frac = static_cast<double>(done) / total;
                g_progress->set_fraction(frac);
                if (detail.empty())
                    g_statusLabel->set_text(
                        Glib::ustring::format("Done ", done, " of ", total));
                else
                    g_statusLabel->set_text("Warning: " + detail);
            });
        } catch (const std::exception& e) {
            done++;
            std::string err = e.what();
            hasErrors = true;
            diagnostics.push_back({true, in, out, err});
            Glib::signal_idle().connect_once([rowPath, done, total, err]() {
                auto row = g_store->get_iter(rowPath);
                if (row) (*row)[g_columns.status] = "FAILED: " + err;
                double frac = static_cast<double>(done) / total;
                g_progress->set_fraction(frac);
                g_statusLabel->set_text(
                    Glib::ustring::format("Error on file ", done, " of ", total));
            });
        }
    }

    const std::string report = liarsoft::gui::formatDiagnostics(diagnostics);
    Glib::signal_idle().connect_once([totalWarnings, hasErrors, report]() {
        g_progress->set_fraction(1.0);
        g_statusLabel->set_text(hasErrors ? "Completed with errors." : totalWarnings == 0
            ? "All done."
            : Glib::ustring::format("Done with ", totalWarnings, " warning(s)."));
        if (!report.empty()) showDiagnostics(report, hasErrors);
        g_convertBtn->set_sensitive(true);
    });
}

// ---- Update output paths when output dir changes ----
static void updateOutputPaths() {
    std::string outDir = g_outDirEntry->get_text();
    const bool gscToTsc = !g_gscToTxtCheck || !g_gscToTxtCheck->get_active();
    const std::string encoding = g_encodingCombo ?
        g_encodingCombo->get_active_id() : "CP932";
    for (auto& child : g_store->children()) {
        const std::string input = static_cast<std::string>(
            static_cast<Glib::ustring>(child[g_columns.inputPath]));
        child[g_columns.outputPath] = liarsoft::gui::guessOutput(
            input, outDir, gscToTsc, encoding);
        child[g_columns.fileType] = liarsoft::gui::guessType(
            input, gscToTsc, encoding);
    }
}

// ---- File chooser dialog ----
static void onAddFiles(Gtk::Window* parent) {
    auto dialog = Gtk::FileChooserDialog("Select Files",
        Gtk::FILE_CHOOSER_ACTION_OPEN);
    dialog.set_transient_for(*parent);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Open", Gtk::RESPONSE_OK);
    dialog.set_select_multiple(true);

    auto filterAll = Gtk::FileFilter::create();
    filterAll->set_name("All Supported");
    filterAll->add_pattern("*.gsc"); filterAll->add_pattern("*.tsc");
    filterAll->add_pattern("*.txt");
    filterAll->add_pattern("*.xfl"); filterAll->add_pattern("*.lwg");
    filterAll->add_pattern("*.wcg"); filterAll->add_pattern("*.lim");
    filterAll->add_pattern("*.webp");
    filterAll->add_pattern("*.wav"); filterAll->add_pattern("*.ogg");
    filterAll->add_pattern("*.exe");
    filterAll->add_pattern("*.png"); filterAll->add_pattern("*.jpg");
    filterAll->add_pattern("*.jpeg"); filterAll->add_pattern("*.bmp");
    dialog.add_filter(filterAll);

    if (dialog.run() == Gtk::RESPONSE_OK) {
        std::string outDir = g_outDirEntry->get_text();
        for (const auto& f : dialog.get_filenames())
            addFiles({f}, outDir);
    }
}

// ---- Choose output directory ----
static void onChooseOutDir(Gtk::Window* parent) {
    auto dialog = Gtk::FileChooserDialog("Select Output Directory",
        Gtk::FILE_CHOOSER_ACTION_SELECT_FOLDER);
    dialog.set_transient_for(*parent);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Select", Gtk::RESPONSE_OK);

    if (dialog.run() == Gtk::RESPONSE_OK) {
        g_outDirEntry->set_text(dialog.get_filename());
        updateOutputPaths();
    }
}

// ---- Choose reference GSC ----
static void onChooseRef(Gtk::Window* parent) {
    auto dialog = Gtk::FileChooserDialog("Select Reference GSC",
        Gtk::FILE_CHOOSER_ACTION_OPEN);
    dialog.set_transient_for(*parent);
    dialog.add_button("Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("Open", Gtk::RESPONSE_OK);
    auto filter = Gtk::FileFilter::create();
    filter->set_name("GSC Files");
    filter->add_pattern("*.gsc");
    dialog.add_filter(filter);

    if (dialog.run() == Gtk::RESPONSE_OK)
        g_refEntry->set_text(dialog.get_filename());
}

// ---- Drag & drop handler ----
static void onDragDataReceived(
    const Glib::RefPtr<Gdk::DragContext>&, int, int,
    const Gtk::SelectionData& sel, guint, guint) {
    auto uris = sel.get_uris();
    std::vector<std::string> paths;
    for (const auto& uri : uris) {
        auto path = Glib::filename_from_uri(uri);
        paths.push_back(path);
    }
    std::string outDir = g_outDirEntry->get_text();
    addFiles(paths, outDir);
}

// ---- Clear list ----
static void onClear() {
    g_store->clear();
    g_progress->set_fraction(0.0);
    g_statusLabel->set_text("Ready.");
}

// ---- Remove selected ----
static void onRemoveSelected(Gtk::TreeView* treeView) {
    auto sel = treeView->get_selection();
    if (auto it = sel->get_selected()) {
        g_store->erase(it);
    }
}

// ---- Main GUI entry point ----
int runGui(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "io.github.liarsofttool");

    Gtk::Window window;
    g_window = &window;
    window.set_title("LiarsoftTool");
    window.set_default_size(800, 500);

    // ---- Layout ----
    auto mainBox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
    mainBox->property_margin() = 8;

    // --- Top bar ---
    auto topBar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    auto encLabel = Gtk::manage(new Gtk::Label("Encoding:"));
    g_encodingCombo = Gtk::manage(new Gtk::ComboBoxText());
    g_encodingCombo->append("CP932", "CP932 / Windows-31J (Japanese)");
    g_encodingCombo->append("GBK", "GBK (Chinese)");
    g_encodingCombo->append("CP1251", "CP1251 (Cyrillic / English)");
    g_encodingCombo->set_active(0);

    auto refLabel = Gtk::manage(new Gtk::Label("Reference:"));
    g_refEntry = Gtk::manage(new Gtk::Entry());
    g_refEntry->set_placeholder_text("Reference GSC file for TXT injection");
    g_refEntry->set_width_chars(20);
    auto refBtn = Gtk::manage(new Gtk::Button("..."));
    refBtn->signal_clicked().connect([&]() { onChooseRef(&window); });

    topBar->pack_start(*encLabel, false, false);
    topBar->pack_start(*g_encodingCombo, false, false);
    topBar->pack_start(*refLabel, false, false);
    topBar->pack_start(*g_refEntry, false, false);
    topBar->pack_start(*refBtn, false, false);
    mainBox->pack_start(*topBar, false, false);

    auto optionsBar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    g_recursiveCheck = Gtk::manage(new Gtk::CheckButton("Recursive"));
    g_recursiveCheck->set_tooltip_text(
        "Recursively pack/unpack archives and convert their resources");
    g_packOnlyCheck = Gtk::manage(new Gtk::CheckButton("Pack only"));
    g_unpackOnlyCheck = Gtk::manage(new Gtk::CheckButton("Unpack only"));
    g_gscToTxtCheck = Gtk::manage(new Gtk::CheckButton("GSC → TXT"));
    g_gscToTxtCheck->set_tooltip_text(
        "Generate legacy TXT instead of structured TSC when decoding GSC files");
    g_gscToTxtCheck->signal_toggled().connect(sigc::ptr_fun(&updateOutputPaths));
    g_encodingCombo->signal_changed().connect(sigc::ptr_fun(&updateOutputPaths));
    optionsBar->pack_start(*g_recursiveCheck, false, false);
    optionsBar->pack_start(*g_packOnlyCheck, false, false);
    optionsBar->pack_start(*g_unpackOnlyCheck, false, false);
    optionsBar->pack_start(*g_gscToTxtCheck, false, false);
    g_vorbisInWavCheck = Gtk::manage(new Gtk::CheckButton("Vorbis-in-WAV"));
    g_vorbisInWavCheck->set_tooltip_text(
        "Keep compressed Vorbis in WAV instead of decoding to 16-bit PCM, including recursive packing. "
        "Neither mode requires an original WAV file.");
    optionsBar->pack_start(*g_vorbisInWavCheck, false, false);
    mainBox->pack_start(*optionsBar, false, false);

    auto workersBar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    auto workersLabel = Gtk::manage(new Gtk::Label("Threads (0 = auto):"));
    g_workersSpin = Gtk::manage(new Gtk::SpinButton());
    g_workersSpin->set_range(0, liarsoft::MaxConversionWorkers);
    g_workersSpin->set_increments(1, 4);
    g_workersSpin->set_numeric(true);
    g_workersSpin->set_value(0);
    g_workersSpin->set_tooltip_text("Recursive resource conversion: 0 = auto (up to 4), 1 = serial. More threads use more memory.");
    workersBar->pack_start(*workersLabel, false, false);
    workersBar->pack_start(*g_workersSpin, false, false);
    mainBox->pack_start(*workersBar, false, false);

    // --- File list ---
    auto scrolled = Gtk::manage(new Gtk::ScrolledWindow());
    scrolled->set_hexpand(true);
    scrolled->set_vexpand(true);

    g_store = Gtk::ListStore::create(g_columns);
    auto treeView = Gtk::manage(new Gtk::TreeView(g_store));
    treeView->append_column("Input", g_columns.inputPath);
    treeView->append_column("Output", g_columns.outputPath);
    treeView->append_column("Type", g_columns.fileType);
    treeView->append_column("Status", g_columns.status);
    // Input + Output split remaining space 50/50
    treeView->get_column(0)->set_expand(true);
    treeView->get_column(1)->set_expand(true);
    // Type + Status fixed width, rightmost; Type wider than Status
    treeView->get_column(2)->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    treeView->get_column(2)->set_fixed_width(140);
    treeView->get_column(3)->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
    treeView->get_column(3)->set_fixed_width(96);

    // Drag & drop
    std::vector<Gtk::TargetEntry> targets; targets.emplace_back("text/uri-list");
    treeView->drag_dest_set(targets, Gtk::DEST_DEFAULT_ALL, Gdk::ACTION_COPY);
    treeView->signal_drag_data_received().connect(sigc::ptr_fun(&onDragDataReceived));

    scrolled->add(*treeView);
    mainBox->pack_start(*scrolled, true, true);

    // --- Bottom bar ---
    auto bottomBar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6));

    auto addBtn = Gtk::manage(new Gtk::Button("Add Files"));
    addBtn->signal_clicked().connect([&]() { onAddFiles(&window); });
    auto removeBtn = Gtk::manage(new Gtk::Button("Remove"));
    removeBtn->signal_clicked().connect([tv = treeView]() { onRemoveSelected(tv); });
    auto clearBtn = Gtk::manage(new Gtk::Button("Clear"));
    clearBtn->signal_clicked().connect(sigc::ptr_fun(&onClear));

    auto outLabel = Gtk::manage(new Gtk::Label("Out Dir:"));
    g_outDirEntry = Gtk::manage(new Gtk::Entry());
    g_outDirEntry->set_placeholder_text("Same as input (default)");
    g_outDirEntry->set_width_chars(15);
    g_outDirEntry->signal_changed().connect(sigc::ptr_fun(&updateOutputPaths));
    auto outBtn = Gtk::manage(new Gtk::Button("..."));
    outBtn->signal_clicked().connect([&]() { onChooseOutDir(&window); });

    g_convertBtn = Gtk::manage(new Gtk::Button("Convert All"));
    g_convertBtn->set_sensitive(false);
    g_convertBtn->signal_clicked().connect([&]() {
        std::string enc = g_encodingCombo->get_active_id();
        std::string ref = g_refEntry->get_text();
        bool recursive = g_recursiveCheck->get_active();
        bool packOnly = g_packOnlyCheck->get_active();
        bool unpackOnly = g_unpackOnlyCheck->get_active();
        bool gscToTsc = !g_gscToTxtCheck->get_active();
        bool vorbisInWav = g_vorbisInWavCheck->get_active();
        g_workersSpin->update();
        const unsigned workers = g_workersSpin->get_value_as_int();
        std::vector<ConversionJob> jobs;
        for (const auto& child : g_store->children()) {
            jobs.push_back({
                static_cast<Glib::ustring>(child[g_columns.inputPath]),
                static_cast<Glib::ustring>(child[g_columns.outputPath]),
                g_store->get_path(child)});
        }
        g_convertBtn->set_sensitive(false);
        std::thread t(convertAll, std::move(jobs), enc, ref, recursive,
                      packOnly, unpackOnly, gscToTsc, vorbisInWav, workers);
        t.detach();
    });

    bottomBar->pack_start(*addBtn, false, false);
    bottomBar->pack_start(*removeBtn, false, false);
    bottomBar->pack_start(*clearBtn, false, false);
    bottomBar->pack_start(*outLabel, false, false);
    bottomBar->pack_start(*g_outDirEntry, false, false);
    bottomBar->pack_start(*outBtn, false, false);
    bottomBar->pack_end(*g_convertBtn, false, false);
    mainBox->pack_start(*bottomBar, false, false);

    // --- Progress bar ---
    g_progress = Gtk::manage(new Gtk::ProgressBar());
    g_progress->set_show_text(false);
    mainBox->pack_start(*g_progress, false, false);

    g_statusLabel = Gtk::manage(new Gtk::Label("Ready. Drag files or click Add Files."));
    mainBox->pack_start(*g_statusLabel, false, false);

    // Enable convert button when there are files
    g_store->signal_row_inserted().connect([&](const Gtk::TreeModel::Path&,
                                                const Gtk::TreeModel::iterator&) {
        g_convertBtn->set_sensitive(g_store->children().size() > 0);
    });
    g_store->signal_row_deleted().connect([&](const Gtk::TreeModel::Path&) {
        g_convertBtn->set_sensitive(g_store->children().size() > 0);
    });

    window.add(*mainBox);
    window.show_all();

    app->run(window);
    return 0;
}
