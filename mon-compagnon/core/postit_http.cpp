#include "postit_http.hpp"
namespace compagnon {
namespace {
NoteHttpResponse error(int status, const char* message) {
    return {status, std::string("{\"error\":\"postit_error\",\"message\":\"") + message + "\"}", false};
}
class Parser {
public:
    explicit Parser(const std::string& input) : s(input) {}
    bool take(char c) { space(); if (p == s.size() || s[p] != c) return false; ++p; return true; }
    bool end() { space(); return p == s.size(); }
    bool word(std::string& out) {
        if (!take('"')) return false;
        while (p < s.size() && s[p] != '"') {
            if (s[p] < 'a' || s[p] > 'z' || out.size() >= 16) return false;
            out += s[p++];
        }
        return take('"');
    }
    bool integer(std::uint64_t& out, std::uint64_t max) {
        space(); if (p == s.size() || s[p] < '0' || s[p] > '9') return false;
        out = 0; const bool zero = s[p] == '0'; unsigned digits = 0;
        while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
            const unsigned d = s[p++] - '0'; ++digits;
            if (d > max || out > (max - d) / 10) return false;
            out = out * 10 + d;
        }
        return !zero || digits == 1;
    }
private:
    void space() { while (p < s.size() && (s[p]==' ' || s[p]=='\n' || s[p]=='\r' || s[p]=='\t')) ++p; }
    const std::string& s; std::size_t p=0;
};
bool parse(const std::string& body, PostitStyle& style) {
    Parser p(body); unsigned seen=0; std::size_t points=0;
    if (!p.take('{')) return false;
    do {
        std::string key; if (!p.word(key) || !p.take(':')) return false;
        const unsigned bit=key=="alignment"?1:key=="strokes"?2:key=="version"?4:0;
        if (!bit || (seen&bit)) return false;
        seen |= bit;
        if (bit==1) {
            std::string value; if (!p.word(value)) return false;
            if (value=="left") style.alignment=PostitAlignment::Left;
            else if (value=="center") style.alignment=PostitAlignment::Center;
            else if (value=="right") style.alignment=PostitAlignment::Right;
            else return false;
        } else if (bit==4) { if (!p.integer(style.version, NoteStore::max_version)) return false; }
        else {
            if (!p.take('[')) return false;
            if (!p.take(']')) {
                do {
                    if (style.strokes.size() >= PostitStore::max_strokes || !p.take('[')) return false;
                    PostitStroke stroke;
                    do {
                        std::uint64_t x,y;
                        if (++points > PostitStore::max_points || !p.take('[') || !p.integer(x,1023) || !p.take(',') || !p.integer(y,1023) || !p.take(']')) return false;
                        stroke.push_back({static_cast<std::uint16_t>(x),static_cast<std::uint16_t>(y)});
                    } while (p.take(','));
                    if (!p.take(']')) return false;
                    style.strokes.push_back(std::move(stroke));
                } while (p.take(','));
                if (!p.take(']')) return false;
            }
        }
    } while (p.take(','));
    return seen==7 && p.take('}') && p.end();
}
std::string json(const PostitStyle& style) {
    const char* alignment=style.alignment==PostitAlignment::Left?"left":style.alignment==PostitAlignment::Right?"right":"center";
    std::string out="{\"alignment\":\""+std::string(alignment)+"\",\"version\":"+std::to_string(style.version)+",\"strokes\":[";
    bool first=true;
    for (const auto& stroke:style.strokes) {
        if (!first) out+=',';
        first=false; out+='['; bool point_first=true;
        for (const auto& point:stroke) { if (!point_first) out+=','; point_first=false; out+='['+std::to_string(point.x)+','+std::to_string(point.y)+']'; }
        out+=']';
    }
    return out+"]}";
}
NoteHttpResponse failure(NoteResult r) {
    return error(r==NoteResult::NotFound?404:r==NoteResult::Conflict?409:r==NoteResult::InvalidArgument?400:r==NoteResult::LimitExceeded?413:503,
        r==NoteResult::Conflict?"Ce post-it a changé. Rechargez sa version.":"Post-it indisponible ou dessin invalide. Aucun succès confirmé.");
}
}
NoteHttpResponse PostitHttp::handle(const std::string& method,const std::string& path,const std::string& query,const std::string& body) {
    if (body.size()>24576) return error(413,"Dessin trop volumineux.");
    if (path.rfind("/api/postits/",0)!=0) return error(404,"Route inconnue.");
    const auto id=path.substr(13);
    if (id.empty() || id.size()>64 || id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos) return error(400,"Identifiant invalide.");
    if (!query.empty()) return error(400,"Requête invalide.");
    if (!notes_.ready() || !styles_.ready()) return failure(NoteResult::NotReady);
    if (!notes_.find(id)) return failure(NoteResult::NotFound);
    PostitStyle style;
    if (method=="GET") {
        if (!body.empty()) return error(400,"Requête invalide.");
        const auto result=styles_.get(id,style);
        if (result==NoteResult::NotFound) { style.note_id=id; return {200,json(style),false}; }
        return result==NoteResult::Ok?NoteHttpResponse{200,json(style),false}:failure(result);
    }
    if (method!="POST" && method!="PATCH") return error(405,"Méthode non autorisée.");
    if (!parse(body,style)) return error(400,"Dessin invalide : 32 traits et 400 points maximum, coordonnées entières de 0 à 1023.");
    PostitStyle saved;
    const auto result=styles_.put(id,style.alignment,style.strokes,style.version,&saved);
    return result==NoteResult::Ok?NoteHttpResponse{200,json(saved),true}:failure(result);
}
}
