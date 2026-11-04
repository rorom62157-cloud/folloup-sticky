#include "postit_http.hpp"
#include "http_access.hpp"
#include <cassert>
#include <map>
#include <iostream>
using namespace compagnon;
struct Files : NoteFiles {
    std::map<std::string,std::string> data; bool fail=false;
    NoteIoResult read(const std::string& n,std::size_t max,std::string& out) override { auto i=data.find(n); if(i==data.end())return NoteIoResult::Missing; if(i->second.size()>max)return NoteIoResult::Error; out=i->second;return NoteIoResult::Ok; }
    NoteIoResult writeExclusive(const std::string& n,const std::string& b) override { if(fail)return NoteIoResult::Error; if(data.count(n))return NoteIoResult::Exists;data[n]=b;return NoteIoResult::Ok; }
    NoteIoResult remove(const std::string& n) override { return data.erase(n)?NoteIoResult::Ok:NoteIoResult::Missing; }
    NoteIoResult renameAbsent(const std::string& a,const std::string& b) override { if(data.count(b))return NoteIoResult::Exists;auto i=data.find(a);if(i==data.end())return NoteIoResult::Missing;data[b]=i->second;data.erase(i);return NoteIoResult::Ok; }
};
int main() {
    Files notesFiles,styleFiles; NoteStore notes(notesFiles);PostitStore styles(styleFiles);
    assert(notes.recover()==NoteResult::Ok && styles.recover()==NoteResult::Ok);
    Note n;assert(notes.create({"Titre","Contenu",false,"audio-original"},&n)==NoteResult::Ok);
    PostitHttp api(notes,styles); const auto path="/api/postits/"+n.id;
    assert(api.handle("GET",path,"","").body=="{\"alignment\":\"center\",\"version\":0,\"strokes\":[]}");
    assert(api.handle("GET","/api/postits/missing","","").status==404);
    assert(api.handle("GET","/api/postits/../x","","").status==400);
    const std::string valid=R"({"alignment":"left","strokes":[[[0,1023],[500,0]]],"version":0})";
    CompanionSession session;HttpAccess access(session,"http://device.local",PairingMode::LocalPassword,true);
    const std::string token(32,'a'),csrf(32,'b');
    HttpAccessRequest request{"POST",path,"device.local","http://device.local","same-origin","mc_session="+token,csrf,valid.size(),true};
    Millis time=0; auto dispatch=[&](){auto allowed=access.check(request,time);return allowed.allowed()?api.handle(request.method,request.path,"",valid):NoteHttpResponse{allowed.status,"",false};};
    assert(dispatch().status==401);assert(access.requestSession(token,csrf,4)==Result::Ok);time=6;
    request.origin="http://foreign.local";assert(dispatch().status==403);request.origin="http://device.local";
    request.csrf="wrong";assert(dispatch().status==403);request.csrf=csrf;
    assert(dispatch().status==200);assert(dispatch().status==409);
    PostitStyle saved;assert(styles.get(n.id,saved)==NoteResult::Ok);assert(saved.alignment==PostitAlignment::Left && saved.strokes[0][0].y==1023);
    assert(notes.find(n.id)->audio_id=="audio-original" && !notes.find(n.id)->pinned && notes.find(n.id)->version==n.version);
    for(const auto* bad:{R"({"alignment":"left","strokes":[],"version":00})",R"({"alignment":"left","strokes":[],"version":0.1})",R"({"alignment":"left","strokes":[],"version":9007199254740992})",R"({"alignment":"left","strokes":[],"version":-1})",R"({"alignment":"left","strokes":[],"version":0,"version":1})",R"({"alignment":"left","strokes":[[]],"version":0})",R"({"alignment":"left","strokes":[[[1024,0]]],"version":0})",R"({"alignment":"left","strokes":[[[1.2,0]]],"version":0})",R"({"alignment":"left","strokes":[],"version":0,"x":1})",R"({"alignment":"left","strokes":[],"version":0}garbage)"})assert(api.handle("POST",path,"",bad).status==400);
    assert(api.handle("POST",path,"",std::string(24577,'x')).status==413);
    std::string tooMany=R"({"alignment":"center","version":0,"strokes":[[)";
    for(int i=0;i<401;i++){if(i)tooMany+=',';tooMany+="[1,2]";}tooMany+="]]}";
    assert(api.handle("POST",path,"",tooMany).status==400);
    std::string tooManyStrokes=R"({"alignment":"center","version":0,"strokes":[)";
    for(int i=0;i<33;i++){if(i)tooManyStrokes+=',';tooManyStrokes+="[[1,2]]";}tooManyStrokes+="]}";
    assert(api.handle("POST",path,"",tooManyStrokes).status==400);
    assert(api.handle("GET",path,"extra=1","").status==400);
    assert(api.handle("DELETE",path,"","").status==405);
    styleFiles.fail=true;
    const auto failed=api.handle("POST",path,"",R"({"alignment":"right","strokes":[],"version":)"+std::to_string(saved.version)+"}");
    assert(failed.status==503 && !failed.mutated);
    assert(session.revoke(7)==Result::Ok);time=8;assert(dispatch().status==401);
    std::cout<<"postit HTTP strict parser, authentication and commit tests passed\n";
}
