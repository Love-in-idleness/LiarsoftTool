#include "gui.h"
#include <gtkmm.h>
#include <iostream>
#include <fstream>
#include <thread>
#include <mutex>
#include <sstream>
#include <filesystem>
#include <algorithm>

#include "gscfile.h"
#include "transfile.h"
#include "xflarchive.h"
#include "wcg_decoder.h"
#include "lim_decoder.h"
#include "lwg_decoder.h"
#include "wav_ogg.h"
#include "exe_patch.h"
#include "stb_image.h"

namespace fs = std::filesystem;

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
static Gtk::Button* g_convertBtn = nullptr;
static Gtk::ProgressBar* g_progress = nullptr;
static Gtk::Label* g_statusLabel = nullptr;
static std::mutex g_mutex;
static bool g_running = false;

// ---- Helpers ----

static std::string getExtension(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    std::string ext = path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext;
}

static std::string replaceExtension(const std::string& path, const std::string& newExt) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) return path + newExt;
    return path.substr(0, pos) + newExt;
}

static Glib::ustring guessOutput(const std::string& inputPath, const std::string& outDir) {
    std::string ext = getExtension(inputPath);
    fs::path in(inputPath);
    fs::path base = outDir.empty() ? in.parent_path() : fs::path(outDir);
    std::string stem = in.stem().string();

    if (ext == ".gsc")      return (base / (stem + ".txt")).string();
    if (ext == ".txt")      return (base / (stem + ".gsc")).string();
    if (ext == ".xfl" || ext == ".lwg") return (base / stem).string();
    if (ext == ".wcg" || ext == ".lim") return (base / (stem + ".png")).string();
    if (ext == ".exe") return (base / (stem + ".gbk.exe")).string();
    if (ext == ".wav")      return (base / (stem + ".ogg")).string();
    if (ext == ".ogg")      return (base / (stem + ".wav")).string();
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
        return (base / (stem + ".wcg")).string();
    return (base / in.filename()).string();
}

static Glib::ustring guessType(const std::string& path) {
    std::string ext = getExtension(path);
    if (ext == ".gsc") return "GSC → TXT";
    if (ext == ".txt") return "TXT → GSC";
    if (ext == ".xfl") return "XFL → DIR";
    if (ext == ".lwg") return "LWG → DIR";
    if (ext == ".wcg") return "WCG → PNG";
    if (ext == ".lim") return "LIM → PNG";
    if (ext == ".exe") return "EXE SJIS→GBK";
    if (ext == ".wav") return "WAV → OGG";
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
        return "IMG → WCG";
    if (fs::is_directory(path)) return "DIR → XFL/LWG";
    return "?";
}

static bool isSupported(const std::string& path) {
    std::string ext = getExtension(path);
    return ext == ".gsc" || ext == ".txt" || ext == ".xfl" || ext == ".lwg" ||
           ext == ".wcg" || ext == ".lim" || ext == ".wav" || ext == ".ogg" || ext == ".exe" ||
           ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" ||
           fs::is_directory(path);
}

// ---- Forward declaration of the conversion worker ----
static void convertOne(const std::string& inputPath,
                       const std::string& outputPath,
                       const std::string& encoding,
                       const std::string& refPath,
                       Gtk::TreeRow* row);

// ---- Add files to the list ----
static void addFiles(const std::vector<std::string>& paths, const std::string& outDir) {
    for (const auto& p : paths) {
        if (!isSupported(p)) continue;
        auto row = *(g_store->append());
        row[g_columns.inputPath]  = p;
        row[g_columns.outputPath] = guessOutput(p, outDir);
        row[g_columns.fileType]   = guessType(p);
        row[g_columns.status]     = "Ready";
    }
}

// ---- Conversion worker thread ----
static void convertAll(const std::string& encoding, const std::string& refPath) {
    g_running = true;
    g_convertBtn->set_sensitive(false);

    auto children = g_store->children();
    int total = children.size();
    int done = 0;

    for (auto& child : children) {
        if (!g_running) break;

        std::string in  = static_cast<Glib::ustring>(child[g_columns.inputPath]);
        std::string out = static_cast<Glib::ustring>(child[g_columns.outputPath]);

        Glib::signal_idle().connect_once([&child, &done, total]() {
            child[g_columns.status] = "Processing...";
            double frac = static_cast<double>(done) / total;
            g_progress->set_fraction(frac);
            g_statusLabel->set_text(
                Glib::ustring::format("Processing ", done + 1, " of ", total));
        });

        // Process this file
        std::string ext = getExtension(in);

        try {
            if (fs::is_directory(in)) {
                bool hasMeta = fs::exists(in + "/.meta.xml");
                if (hasMeta)
                    liarsoft::LwgPacker::packToFile(in, out, encoding);
                else {
                    liarsoft::XflArchive arch;
                    arch.encoding = encoding;
                    arch.addDirectory(in);
                    arch.save(out);
                }
            } else if (ext == ".gsc") {
                auto gsc = liarsoft::GscFile::fromFile(in, encoding);
                auto trans = liarsoft::TransFile::fromGsc(gsc);
                trans.save(out);
            } else if (ext == ".txt") {
                std::string ref = refPath.empty() ? replaceExtension(in, ".gsc") : refPath;
                auto trans = liarsoft::TransFile::fromFile(in);
                auto gsc = trans.toGsc(ref, encoding);
                gsc.save(out);
            } else if (ext == ".xfl") {
                auto arch = liarsoft::XflArchive::fromFile(in, encoding);
                arch.extractToDirectory(out);
            } else if (ext == ".lwg") {
                std::ifstream fs(in, std::ios::binary);
                fs.seekg(0, std::ios::end);
                size_t sz = fs.tellg(); fs.seekg(0, std::ios::beg);
                std::vector<uint8_t> raw(sz);
                fs.read(reinterpret_cast<char*>(raw.data()), sz);
                auto arch = liarsoft::LwgDecoder::decode(raw, encoding);
                liarsoft::LwgDecoder::extractToDirectory(arch, out, encoding);
            } else if (ext == ".wcg") {
                std::ifstream fs(in, std::ios::binary);
                fs.seekg(0, std::ios::end);
                size_t sz = fs.tellg(); fs.seekg(0, std::ios::beg);
                std::vector<uint8_t> raw(sz);
                fs.read(reinterpret_cast<char*>(raw.data()), sz);
                auto img = liarsoft::wcgDecode(raw);
                liarsoft::wcgSavePng(img, out);
            } else if (ext == ".lim") {
                std::ifstream fs(in, std::ios::binary);
                fs.seekg(0, std::ios::end);
                size_t sz = fs.tellg(); fs.seekg(0, std::ios::beg);
                std::vector<uint8_t> raw(sz);
                fs.read(reinterpret_cast<char*>(raw.data()), sz);
                auto img = liarsoft::limDecode(raw);
                liarsoft::limSavePng(img, out);
            } else if (ext == ".wav") {
                liarsoft::WavOggExtractor::extractToFile(in, out);
            } else if (ext == ".ogg") {
                std::string ref = refPath.empty() ? replaceExtension(in, ".wav") : refPath;
                liarsoft::WavOggExtractor::embedToFile(in, ref, out);
            } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp") {
                int w, h, ch;
                unsigned char* px = stbi_load(in.c_str(), &w, &h, &ch, 4);
                if (!px) throw std::runtime_error("Failed to load image");
                auto wcgData = liarsoft::wcgEncode(px, static_cast<uint32_t>(w), static_cast<uint32_t>(h));
                stbi_image_free(px);
                std::ofstream fout(out, std::ios::binary);
                fout.write(reinterpret_cast<const char*>(wcgData.data()), wcgData.size());
            } else if (ext == ".exe") {
                liarsoft::exeConvertFile(in, out, encoding);
            } else {
                throw std::runtime_error("Unsupported format");
            }

            done++;
            Glib::signal_idle().connect_once([&child, &done, total]() {
                child[g_columns.status] = "OK";
                double frac = static_cast<double>(done) / total;
                g_progress->set_fraction(frac);
                g_statusLabel->set_text(
                    Glib::ustring::format("Done ", done, " of ", total));
            });
        } catch (const std::exception& e) {
            done++;
            std::string err = e.what();
            Glib::signal_idle().connect_once([&child, &done, total, err]() {
                child[g_columns.status] = "FAILED: " + err;
                double frac = static_cast<double>(done) / total;
                g_progress->set_fraction(frac);
                g_statusLabel->set_text(
                    Glib::ustring::format("Error on file ", done, " of ", total));
            });
        }
    }

    Glib::signal_idle().connect_once([]() {
        g_progress->set_fraction(1.0);
        g_statusLabel->set_text("All done.");
        g_convertBtn->set_sensitive(true);
        g_running = false;
    });
}

// ---- Update output paths when output dir changes ----
static void updateOutputPaths() {
    std::string outDir = g_outDirEntry->get_text();
    for (auto& child : g_store->children()) {
        child[g_columns.outputPath] = guessOutput(static_cast<std::string>(static_cast<Glib::ustring>(child[g_columns.inputPath])), outDir);
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
    filterAll->add_pattern("*.gsc"); filterAll->add_pattern("*.txt");
    filterAll->add_pattern("*.xfl"); filterAll->add_pattern("*.lwg");
    filterAll->add_pattern("*.wcg"); filterAll->add_pattern("*.lim");
    filterAll->add_pattern("*.wav");
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
static void onRemoveSelected(Glib::RefPtr<Gtk::TreeView> treeView) {
    auto sel = treeView->get_selection();
    if (auto it = sel->get_selected()) {
        g_store->erase(it);
    }
}

// ---- Main GUI entry point ----
int runGui(int argc, char* argv[]) {
    auto app = Gtk::Application::create(argc, argv, "io.github.liarsofttool");

    Gtk::Window window;
    window.set_title("LiarsoftTool");
    window.set_default_size(800, 500);

    // ---- Layout ----
    auto mainBox = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4));
    mainBox->property_margin() = 8;

    // --- Top bar ---
    auto topBar = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
    auto encLabel = Gtk::manage(new Gtk::Label("Encoding:"));
    g_encodingCombo = Gtk::manage(new Gtk::ComboBoxText());
    g_encodingCombo->append("SHIFT_JIS", "Shift-JIS (Japanese)");
    g_encodingCombo->append("GBK", "GBK (Chinese)");
    g_encodingCombo->append("CP1251", "CP1251 (Cyrillic / English)");
    g_encodingCombo->set_active(0);

    auto refLabel = Gtk::manage(new Gtk::Label("Ref GSC:"));
    g_refEntry = Gtk::manage(new Gtk::Entry());
    g_refEntry->set_placeholder_text("Reference GSC for TXT→GSC");
    g_refEntry->set_width_chars(20);
    auto refBtn = Gtk::manage(new Gtk::Button("..."));
    refBtn->signal_clicked().connect([&]() { onChooseRef(&window); });

    topBar->pack_start(*encLabel, false, false);
    topBar->pack_start(*g_encodingCombo, false, false);
    topBar->pack_start(*refLabel, false, false);
    topBar->pack_start(*g_refEntry, false, false);
    topBar->pack_start(*refBtn, false, false);
    mainBox->pack_start(*topBar, false, false);

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
    removeBtn->signal_clicked().connect([tv = treeView]() { onRemoveSelected(Glib::RefPtr<Gtk::TreeView>(tv)); });
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
        std::thread t(convertAll, enc, ref);
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
