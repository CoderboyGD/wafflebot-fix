// Macro loading for WaffleBot.
//
// Formats supported:
//   GDR (JSON)         .json / .gdr.json   xdBot, MH Replay 2.2, GDH, Eclipse, ... (GDReplayFormat v1)
//   GDR (MessagePack)  .gdr
//   GDR2 (binary)      .gdr2
//   TASbot             .json
//   Mega Hack Replay   .mhr.json, .mhr (binary "HACK" files)
//   xdBot (old text)   .xd
//   zBot Frame         .zbf
//   Plaintext          .txt
//   Silicate (v1)      .slc
//
// The binary layouts follow the public format descriptions used by Zeo's Clickbot
// (zcb3, Unlicense / public domain): https://github.com/zeozeozeo/zcb3

#include "MacroLoader.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace {

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// ---------------------------------------------------------------- binary reader

class Reader {
public:
    Reader(const QByteArray& data)
        : m_d(reinterpret_cast<const uint8_t*>(data.constData())), m_n(static_cast<size_t>(data.size())) {}

    size_t pos() const { return m_pos; }
    size_t remaining() const { return m_n - m_pos; }
    void seek(size_t p) {
        if (p > m_n) throw FormatError("file is truncated");
        m_pos = p;
    }
    void skip(size_t k) {
        need(k);
        m_pos += k;
    }
    uint8_t u8() {
        need(1);
        return m_d[m_pos++];
    }
    uint32_t u32le() {
        need(4);
        uint32_t v = m_d[m_pos] | (m_d[m_pos + 1] << 8) | (m_d[m_pos + 2] << 16) | (uint32_t(m_d[m_pos + 3]) << 24);
        m_pos += 4;
        return v;
    }
    int32_t i32le() { return static_cast<int32_t>(u32le()); }
    uint32_t u32be() {
        need(4);
        uint32_t v = (uint32_t(m_d[m_pos]) << 24) | (m_d[m_pos + 1] << 16) | (m_d[m_pos + 2] << 8) | m_d[m_pos + 3];
        m_pos += 4;
        return v;
    }
    uint64_t u64le() {
        uint64_t lo = u32le();
        uint64_t hi = u32le();
        return lo | (hi << 32);
    }
    uint64_t u64be() {
        uint64_t hi = u32be();
        uint64_t lo = u32be();
        return (hi << 32) | lo;
    }
    float f32le() {
        uint32_t v = u32le();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    float f32be() {
        uint32_t v = u32be();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    double f64le() {
        uint64_t v = u64le();
        double f;
        std::memcpy(&f, &v, 8);
        return f;
    }
    double f64be() {
        uint64_t v = u64be();
        double f;
        std::memcpy(&f, &v, 8);
        return f;
    }
    // GDR2 varint: little-endian base-128, max 9 bytes
    uint64_t varint() {
        uint64_t result = 0;
        for (unsigned i = 0; i < 9; ++i) {
            uint8_t b = u8();
            result |= static_cast<uint64_t>(b & 0x7F) << (i * 7);
            if (!(b & 0x80)) return result;
        }
        throw FormatError("invalid varint");
    }
    std::string str() {
        uint64_t len = varint();
        if (len > 0xFFFF) throw FormatError("string too long");
        need(static_cast<size_t>(len));
        std::string s(reinterpret_cast<const char*>(m_d + m_pos), static_cast<size_t>(len));
        m_pos += static_cast<size_t>(len);
        return s;
    }

private:
    void need(size_t k) const {
        if (m_pos + k > m_n) throw FormatError("unexpected end of file (file truncated or wrong format)");
    }
    const uint8_t* m_d;
    size_t m_n;
    size_t m_pos{0};
};

int normalizeButton(int64_t b) {
    if (b == 2) return 2;
    if (b == 3) return 3;
    return 1;
}

// ---------------------------------------------------------------- MessagePack -> QJsonValue

QJsonValue msgpackValue(Reader& r, int depth);

QJsonValue msgpackArray(Reader& r, uint64_t count, int depth) {
    QJsonArray arr;
    for (uint64_t i = 0; i < count; ++i) arr.append(msgpackValue(r, depth + 1));
    return arr;
}

QJsonValue msgpackMap(Reader& r, uint64_t count, int depth) {
    QJsonObject obj;
    for (uint64_t i = 0; i < count; ++i) {
        QJsonValue k = msgpackValue(r, depth + 1);
        QJsonValue v = msgpackValue(r, depth + 1);
        obj.insert(k.isString() ? k.toString() : QString::number(i), v);
    }
    return obj;
}

QJsonValue msgpackString(Reader& r, uint64_t len) {
    if (len > r.remaining()) throw FormatError("string runs past end of file");
    QByteArray bytes;
    bytes.resize(static_cast<qsizetype>(len));
    for (uint64_t i = 0; i < len; ++i) bytes[static_cast<qsizetype>(i)] = static_cast<char>(r.u8());
    return QString::fromUtf8(bytes);
}

QJsonValue msgpackValue(Reader& r, int depth) {
    if (depth > 32) throw FormatError("MessagePack nesting too deep");
    uint8_t t = r.u8();
    if (t <= 0x7f) return static_cast<int>(t);
    if (t >= 0xe0) return static_cast<int>(static_cast<int8_t>(t));
    if (t >= 0x80 && t <= 0x8f) return msgpackMap(r, t & 0x0f, depth);
    if (t >= 0x90 && t <= 0x9f) return msgpackArray(r, t & 0x0f, depth);
    if (t >= 0xa0 && t <= 0xbf) return msgpackString(r, t & 0x1f);
    switch (t) {
    case 0xc0: return QJsonValue(QJsonValue::Null);
    case 0xc2: return false;
    case 0xc3: return true;
    case 0xc4: { uint64_t n = r.u8(); r.skip(n); return QJsonValue(QJsonValue::Null); }
    case 0xc5: { uint64_t n = (uint64_t(r.u8()) << 8) | r.u8(); r.skip(n); return QJsonValue(QJsonValue::Null); }
    case 0xc6: { uint64_t n = r.u32be(); r.skip(n); return QJsonValue(QJsonValue::Null); }
    case 0xca: return static_cast<double>(r.f32be());
    case 0xcb: return r.f64be();
    case 0xcc: return static_cast<int>(r.u8());
    case 0xcd: { int v = r.u8() << 8; v |= r.u8(); return v; }
    case 0xce: return static_cast<double>(r.u32be());
    case 0xcf: return static_cast<double>(r.u64be());
    case 0xd0: return static_cast<int>(static_cast<int8_t>(r.u8()));
    case 0xd1: { int v = r.u8() << 8; v |= r.u8(); return static_cast<int>(static_cast<int16_t>(v)); }
    case 0xd2: return static_cast<int>(static_cast<int32_t>(r.u32be()));
    case 0xd3: return static_cast<double>(static_cast<int64_t>(r.u64be()));
    case 0xd9: return msgpackString(r, r.u8());
    case 0xda: { uint64_t n = (uint64_t(r.u8()) << 8) | r.u8(); return msgpackString(r, n); }
    case 0xdb: return msgpackString(r, r.u32be());
    case 0xdc: { uint64_t n = (uint64_t(r.u8()) << 8) | r.u8(); return msgpackArray(r, n, depth); }
    case 0xdd: return msgpackArray(r, r.u32be(), depth);
    case 0xde: { uint64_t n = (uint64_t(r.u8()) << 8) | r.u8(); return msgpackMap(r, n, depth); }
    case 0xdf: return msgpackMap(r, r.u32be(), depth);
    // ext types: skip payload
    case 0xd4: r.skip(2); return QJsonValue(QJsonValue::Null);
    case 0xd5: r.skip(3); return QJsonValue(QJsonValue::Null);
    case 0xd6: r.skip(5); return QJsonValue(QJsonValue::Null);
    case 0xd7: r.skip(9); return QJsonValue(QJsonValue::Null);
    case 0xd8: r.skip(17); return QJsonValue(QJsonValue::Null);
    case 0xc7: { uint64_t n = r.u8(); r.skip(n + 1); return QJsonValue(QJsonValue::Null); }
    case 0xc8: { uint64_t n = (uint64_t(r.u8()) << 8) | r.u8(); r.skip(n + 1); return QJsonValue(QJsonValue::Null); }
    case 0xc9: { uint64_t n = r.u32be(); r.skip(n + 1); return QJsonValue(QJsonValue::Null); }
    default: throw FormatError("unsupported MessagePack type byte");
    }
}

// ---------------------------------------------------------------- JSON based formats

void parseGdrObject(const QJsonObject& root, RawMacro& m, const char* encoding) {
    QString botName = root.value("bot").toObject().value("name").toString();

    m.fps = root.contains("framerate") ? root.value("framerate").toDouble(240.0) : 240.0;
    if (!(m.fps > 0)) m.fps = 240.0;
    m.format = std::string("GDR (") + encoding + ")";
    if (!botName.isEmpty()) m.format += ", made by " + botName.toStdString();
    m.swapPlayers = (botName == "xdBot");

    QJsonArray inputs = root.value("inputs").toArray();
    m.inputs.reserve(static_cast<size_t>(inputs.size()));
    for (const QJsonValue& v : inputs) {
        QJsonObject o = v.toObject();
        if (!o.contains("frame")) continue;
        RawInput in;
        in.frame = static_cast<int64_t>(std::llround(o.value("frame").toDouble()));
        in.button = normalizeButton(static_cast<int64_t>(o.value("btn").toDouble(1)));
        in.player2 = o.value("2p").toBool(false);
        in.down = o.value("down").toBool(false);
        m.inputs.push_back(in);
    }
}

void parseMhrJson(const QJsonObject& root, RawMacro& m) {
    m.format = "Mega Hack Replay (JSON)";
    m.fps = root.value("meta").toObject().value("fps").toDouble(0);
    QJsonArray events = root.value("events").toArray();
    m.inputs.reserve(static_cast<size_t>(events.size()));
    for (const QJsonValue& v : events) {
        QJsonObject o = v.toObject();
        if (!o.contains("down") || !o.contains("frame")) continue; // not an input event
        RawInput in;
        in.frame = static_cast<int64_t>(std::llround(o.value("frame").toDouble()));
        in.down = o.value("down").toBool(false);
        in.player2 = o.value("p2").toBool(false);
        in.button = 1;
        m.inputs.push_back(in);
    }
}

void parseTasbot(const QJsonObject& root, RawMacro& m) {
    m.format = "TASbot (JSON)";
    m.fps = root.value("fps").toDouble(0);
    QJsonArray events = root.value("macro").toArray();
    int prevP1 = 0, prevP2 = 0;
    for (const QJsonValue& v : events) {
        QJsonObject o = v.toObject();
        int64_t frame = static_cast<int64_t>(std::llround(o.value("frame").toDouble()));
        int p1 = static_cast<int>(o.value("player_1").toObject().value("click").toDouble(0));
        int p2 = static_cast<int>(o.value("player_2").toObject().value("click").toDouble(0));

        // 0 = nothing, 1 = click, 2 = release.
        // Two clicks in a row means there was a release in between.
        if (p1 != 0) {
            if (p1 == 1 && prevP1 == 1) m.inputs.push_back({frame, 1, false, false});
            m.inputs.push_back({frame, 1, false, p1 == 1});
        }
        if (p2 != 0) {
            if (p2 == 1 && prevP2 == 1) m.inputs.push_back({frame, 1, true, false});
            m.inputs.push_back({frame, 1, true, p2 == 1});
        }
        prevP1 = p1;
        prevP2 = p2;
    }
}

void parseJsonRoot(const QJsonObject& root, RawMacro& m, const char* encoding) {
    if (root.contains("inputs")) {
        parseGdrObject(root, m, encoding);
    } else if (root.contains("events") && root.value("meta").isObject()) {
        parseMhrJson(root, m);
    } else if (root.contains("macro") && root.contains("fps")) {
        parseTasbot(root, m);
    } else {
        throw FormatError("this JSON file isn't a known macro (no 'inputs', 'events' or 'macro' list found)");
    }
}

// ---------------------------------------------------------------- binary formats

void parseGdr2(Reader& r, RawMacro& m) {
    r.skip(3); // "GDR"
    uint64_t version = r.varint();
    if (version != 2) throw FormatError("unsupported GDR version " + std::to_string(version));

    std::string inputTag = r.str();
    bool hasExtension = !inputTag.empty();

    r.str();           // author
    r.str();           // description
    r.f32be();         // duration
    r.varint();        // game version
    double framerate = r.f64be();
    r.varint();        // seed
    r.varint();        // coins
    r.u8();            // ldm
    bool platformer = r.u8() != 0;
    std::string botName = r.str();
    r.varint();        // bot version
    r.varint();        // level id
    r.str();           // level name

    uint64_t extSize = r.varint();
    r.skip(static_cast<size_t>(extSize));

    uint64_t deathCount = r.varint();
    for (uint64_t i = 0; i < deathCount; ++i) r.varint();

    uint64_t total = r.varint();
    uint64_t p1Remaining = r.varint();

    m.fps = framerate;
    m.format = "GDR2 (binary)";
    if (!botName.empty()) m.format += ", made by " + botName;

    uint64_t prev = 0;
    while (r.remaining() > 0 && m.inputs.size() < total) {
        uint64_t packed = r.varint();
        uint64_t delta = platformer ? (packed >> 3) : (packed >> 1);
        uint64_t frame = prev + delta;
        int button = platformer ? static_cast<int>((packed >> 1) & 3) : 1;
        bool p2 = (p1Remaining == 0);
        bool down = (packed & 1) != 0;

        RawInput in{static_cast<int64_t>(frame), normalizeButton(button), p2, down};

        if (hasExtension) {
            if (r.remaining() == 0) {
                m.inputs.push_back(in);
                break;
            }
            uint64_t sz = r.varint();
            if (sz > 0) {
                if (sz > r.remaining()) {
                    m.inputs.push_back(in);
                    break;
                }
                r.skip(static_cast<size_t>(sz));
            }
        }

        prev = frame;
        m.inputs.push_back(in);
        if (p1Remaining > 0) {
            --p1Remaining;
            if (p1Remaining == 0) prev = 0;
        }
    }
}

void parseMhrBinary(Reader& r, RawMacro& m) {
    r.seek(0);
    if (r.u32be() != 0x4841434B) throw FormatError("not a Mega Hack binary replay (bad header)");
    m.format = "Mega Hack Replay (binary)";
    r.seek(12);
    m.fps = r.u32le();
    r.seek(28);
    uint32_t count = r.u32le();
    // each record: 2 bytes, down, player, frame(u32), 24 bytes = 32 bytes
    size_t maxRecords = r.remaining() / 32;
    if (count > maxRecords) count = static_cast<uint32_t>(maxRecords);
    for (uint32_t i = 0; i < count; ++i) {
        r.skip(2);
        bool down = r.u8() == 1;
        bool p1 = r.u8() == 0;
        uint32_t frame = r.u32le();
        r.skip(24);
        m.inputs.push_back({static_cast<int64_t>(frame), 1, !p1, down});
    }
}

void parseZbf(Reader& r, RawMacro& m) {
    m.format = "zBot Frame (.zbf)";
    float delta = r.f32le();
    float speedhack = r.f32le();
    if (speedhack == 0.0f) speedhack = 1.0f;
    if (delta == 0.0f) throw FormatError("zBot file has a frame delta of 0");
    m.fps = 1.0 / static_cast<double>(delta) / static_cast<double>(speedhack);
    while (r.remaining() >= 6) {
        int32_t frame = r.i32le();
        bool down = r.u8() == 0x31;
        bool p1 = r.u8() == 0x31;
        m.inputs.push_back({frame, 1, !p1, down});
    }
}

void parseSilicateV1(Reader& r, RawMacro& m) {
    m.format = "Silicate (.slc)";
    m.fps = r.f64le();
    uint32_t count = r.u32le();
    size_t maxCount = r.remaining() / 4;
    if (count > maxCount) count = static_cast<uint32_t>(maxCount);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t a = r.u32le();
        int64_t frame = a >> 4;
        bool p2 = (a & 0b1000) != 0;
        bool down = (a & 1) != 0;
        int button = normalizeButton((a & 0b0110) >> 1);
        m.inputs.push_back({frame, button, p2, down});
    }
}

// ---------------------------------------------------------------- text formats

QStringList splitLines(const QByteArray& data) {
    QString text = QString::fromUtf8(data);
    text.remove(QChar(0xFEFF));
    return text.split('\n');
}

void parseXdText(const QStringList& lines, RawMacro& m) {
    m.format = "xdBot (.xd text)";
    m.fps = 240.0;
    for (const QString& raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        QStringList f = line.split('|');
        if (f.size() == 1) {
            bool ok = false;
            double fps = f[0].toDouble(&ok);
            if (ok) m.fps = fps;
            continue;
        }
        if (f.size() < 4) continue;
        bool okFrame = false;
        int64_t frame = f[0].toLongLong(&okFrame);
        if (!okFrame) continue;
        bool down = f[1].trimmed() == "1";
        int button = f[2].trimmed().toInt();
        bool p1 = f[3].trimmed() == "1";
        m.inputs.push_back({frame, normalizeButton(button), !p1, down});
    }
}

void parsePlaintext(const QStringList& lines, RawMacro& m) {
    m.format = "Plaintext (.txt)";
    bool first = true;
    for (const QString& raw : lines) {
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (first) {
            bool ok = false;
            m.fps = line.toDouble(&ok);
            if (!ok) throw FormatError("first line of a .txt macro must be the FPS");
            first = false;
            continue;
        }
        QStringList f = line.split(' ', Qt::SkipEmptyParts);
        if (f.size() < 4) continue;
        bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
        double frame = f[0].toDouble(&ok1);
        int down = f[1].toInt(&ok2);
        int button = f[2].toInt(&ok3);
        int p1flag = f[3].toInt(&ok4);
        if (!(ok1 && ok2 && ok3 && ok4)) continue;
        m.inputs.push_back({static_cast<int64_t>(frame), normalizeButton(button), p1flag == 0, down == 1});
    }
}

} // namespace

// ---------------------------------------------------------------- entry point

const char* supportedMacroFilter() {
    return "Geometry Dash Macros (*.json *.gdr *.gdr2 *.mhr *.xd *.zbf *.slc *.txt);;All files (*)";
}

bool loadRawMacro(const std::string& path, RawMacro& out, std::string& error) {
    out = RawMacro{};
    try {
        QString qpath = QString::fromStdString(path);
        QFile file(qpath);
        if (!file.open(QIODevice::ReadOnly)) {
            throw FormatError("couldn't open the file (" + file.errorString().toStdString() + ")");
        }
        QByteArray data = file.readAll();
        file.close();
        if (data.isEmpty()) throw FormatError("the file is empty");

        QString ext = QFileInfo(qpath).suffix().toLower();
        QString fullName = QFileInfo(qpath).fileName().toLower();
        Reader r(data);
        uint8_t first = static_cast<uint8_t>(data[0]);

        // skip whitespace / BOM to find the first meaningful byte (for JSON detection)
        int idx = 0;
        if (data.startsWith("\xEF\xBB\xBF")) idx = 3;
        while (idx < data.size() && (data[idx] == ' ' || data[idx] == '\t' || data[idx] == '\r' || data[idx] == '\n')) ++idx;
        char firstText = idx < data.size() ? data[idx] : 0;

        if (data.startsWith("GDR")) {
            parseGdr2(r, out);
        } else if (data.size() >= 4 && data.startsWith("HACK")) {
            parseMhrBinary(r, out);
        } else if (data.startsWith("SILL") || data.startsWith("SLC3")) {
            throw FormatError("Silicate 2/3 macros (.slc2/.slc3) aren't supported yet - only the original .slc format");
        } else if (ext == "zbf") {
            // checked by extension before the content sniffing below: a zBot file starts with a float,
            // whose first byte can look like a MessagePack header
            parseZbf(r, out);
        } else if (ext == "slc") {
            parseSilicateV1(r, out);
        } else if (firstText == '{') {
            QJsonParseError pe;
            QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
            if (doc.isNull() || !doc.isObject()) {
                throw FormatError("invalid JSON: " + pe.errorString().toStdString());
            }
            parseJsonRoot(doc.object(), out, "JSON");
        } else if ((first >= 0x80 && first <= 0x8f) || first == 0xde || first == 0xdf) {
            QJsonValue v = msgpackValue(r, 0);
            if (!v.isObject()) throw FormatError("MessagePack file doesn't contain a macro");
            parseJsonRoot(v.toObject(), out, "MessagePack");
        } else if (data.contains('|')) {
            parseXdText(splitLines(data), out);
        } else if (ext == "txt" || ext == "xd") {
            parsePlaintext(splitLines(data), out);
        } else {
            throw FormatError("unrecognised macro format ('." + ext.toStdString() + "')");
        }

        if (!std::isfinite(out.fps) || out.fps <= 0) throw FormatError("the macro has an invalid FPS value");
        if (out.inputs.empty()) throw FormatError("the macro contains no clicks");
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        out = RawMacro{};
        return false;
    }
}
