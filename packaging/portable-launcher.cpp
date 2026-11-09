// Stel native single-file container launcher. GPL-3.0-or-later.
// The payload is produced by scripts/package-single.py; no shell is involved.
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#else
#include <zlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#endif

namespace fs=std::filesystem;
namespace {
constexpr char magic[16]={'S','T','E','L','_','P','O','R','T','A','B','L','E','_','0','1'};
uint64_t integer(const unsigned char *p,int size) {uint64_t result=0;for(int n=size-1;n>=0;--n)result=(result<<8)|p[n];return result;}
fs::path self() {
#ifdef _WIN32
    std::vector<wchar_t> path(32768);const auto count=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));
    if(!count||count==path.size())throw std::runtime_error("Cannot locate the Stel executable.");
    return std::wstring(path.data(),count);
#else
    std::array<char,4096> path{};const auto count=readlink("/proc/self/exe",path.data(),path.size());
    if(count<=0||size_t(count)==path.size())throw std::runtime_error("Cannot locate the Stel executable.");
    return std::string(path.data(),size_t(count));
#endif
}
struct Temporary {
    fs::path path;
    Temporary() {
#ifdef _WIN32
        wchar_t base[MAX_PATH+1],file[MAX_PATH+1];
        const auto length=GetTempPathW(MAX_PATH,base);
        if(!length||length>=MAX_PATH||!GetTempFileNameW(base,L"stl",0,file))throw std::runtime_error("Cannot create Stel's temporary directory.");
        DeleteFileW(file);if(!CreateDirectoryW(file,nullptr))throw std::runtime_error("Cannot create Stel's temporary directory.");path=file;
#else
        std::string name=(fs::temp_directory_path()/"stel-XXXXXX").string();
        std::vector<char> buffer(name.begin(),name.end());buffer.push_back(0);
        const auto result=mkdtemp(buffer.data());if(!result)throw std::runtime_error("Cannot create Stel's temporary directory.");path=result;
#endif
    }
    ~Temporary(){std::error_code ignored;fs::remove_all(path,ignored);}
};
class Reader {
    std::ifstream file_;
    uint64_t remaining_;
    bool compressed_;
    bool ended_=false;
#ifndef _WIN32
    z_stream stream_{};
    std::array<unsigned char,65536> input_{};
#endif
public:
    Reader(const fs::path &path,uint64_t offset,uint64_t length,bool compressed):file_(path,std::ios::binary),remaining_(length),compressed_(compressed) {
        file_.seekg(std::streamoff(offset));if(!file_)throw std::runtime_error("Cannot read Stel's payload.");
#ifdef _WIN32
        if(compressed_)throw std::runtime_error("Unsupported container format.");
#else
        if(compressed_&&inflateInit(&stream_)!=Z_OK)throw std::runtime_error("Cannot initialize the container reader.");
#endif
    }
    ~Reader(){
#ifndef _WIN32
        if(compressed_)inflateEnd(&stream_);
#endif
    }
    void read(void *data,size_t size) {
        auto *output=static_cast<unsigned char*>(data);
        if(!compressed_) {
            if(size>remaining_)throw std::runtime_error("Truncated Stel payload.");
            file_.read(reinterpret_cast<char*>(output),std::streamsize(size));remaining_-=size;
            if(!file_)throw std::runtime_error("Truncated Stel payload.");
            return;
        }
#ifndef _WIN32
        stream_.next_out=output;stream_.avail_out=unsigned(size);
        while(stream_.avail_out) {
            if(!stream_.avail_in) {
                const auto count=size_t(std::min<uint64_t>(remaining_,input_.size()));
                if(!count)throw std::runtime_error("Truncated Stel payload.");
                file_.read(reinterpret_cast<char*>(input_.data()),std::streamsize(count));if(!file_)throw std::runtime_error("Truncated Stel payload.");
                remaining_-=count;stream_.next_in=input_.data();stream_.avail_in=unsigned(count);
            }
            const auto result=inflate(&stream_,Z_NO_FLUSH);
            ended_=result==Z_STREAM_END;
            if(result==Z_STREAM_END&&stream_.avail_out)throw std::runtime_error("Truncated Stel payload.");
            if(result!=Z_OK&&result!=Z_STREAM_END)throw std::runtime_error("Damaged Stel payload.");
        }
#endif
    }
    void finish() {
        if(!compressed_){if(remaining_)throw std::runtime_error("Invalid Stel payload tail.");return;}
#ifndef _WIN32
        unsigned char extra;
        while(!ended_) {
            if(!stream_.avail_in) {
                const auto count=size_t(std::min<uint64_t>(remaining_,input_.size()));if(!count)throw std::runtime_error("Truncated Stel payload checksum.");
                file_.read(reinterpret_cast<char*>(input_.data()),std::streamsize(count));if(!file_)throw std::runtime_error("Truncated Stel payload.");
                remaining_-=count;stream_.next_in=input_.data();stream_.avail_in=unsigned(count);
            }
            stream_.next_out=&extra;stream_.avail_out=1;const auto result=inflate(&stream_,Z_NO_FLUSH);
            if(stream_.avail_out==0)throw std::runtime_error("Invalid Stel payload tail.");
            if(result!=Z_OK&&result!=Z_STREAM_END)throw std::runtime_error("Damaged Stel payload checksum.");
            ended_=result==Z_STREAM_END;
        }
        if(remaining_||stream_.avail_in)throw std::runtime_error("Invalid Stel payload tail.");
#endif
    }
};
void extract(Reader &reader,const fs::path &root) {
    std::array<char,65536> buffer{};uint64_t total=0;
    for(unsigned files=0;files<30000;++files) {
        unsigned char header[16];reader.read(header,sizeof(header));
        const auto nameLength=integer(header,4),mode=integer(header+4,4),length=integer(header+8,8);
        if(nameLength==0){if(length||mode)throw std::runtime_error("Invalid Stel payload.");return;}
        if(nameLength>4096||length>uint64_t(1024)*1024*1024||(total+=length)>uint64_t(4)*1024*1024*1024)throw std::runtime_error("Invalid Stel payload size.");
        std::string name(size_t(nameLength),'\0');reader.read(name.data(),name.size());
        if(name.find('\0')!=std::string::npos||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos)throw std::runtime_error("Invalid container path.");
        const auto relative=fs::u8path(name);if(relative.is_absolute()||relative.has_root_name())throw std::runtime_error("Invalid container path.");
        for(const auto &part:relative)if(part==".."||part==".")throw std::runtime_error("Invalid container path.");
        const auto path=root/relative;fs::create_directories(path.parent_path());
        std::ofstream output(path,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("Cannot unpack Stel. Check temporary disk space.");
        for(uint64_t remaining=length;remaining;) {
            const auto count=size_t(std::min<uint64_t>(remaining,buffer.size()));reader.read(buffer.data(),count);output.write(buffer.data(),std::streamsize(count));remaining-=count;
        }
        output.close();if(!output)throw std::runtime_error("Cannot unpack Stel. Check temporary disk space.");
#ifndef _WIN32
        if(chmod(path.c_str(),mode_t(mode&0777))!=0)throw std::runtime_error("Cannot set executable permissions.");
#endif
    }
    throw std::runtime_error("Too many container files.");
}
#ifdef _WIN32
std::wstring quote(const std::wstring &arg) {
    std::wstring result=L"\"";size_t slashes=0;
    for(auto c:arg){if(c==L'\\'){++slashes;continue;}if(c==L'\"'){result.append(slashes*2+1,L'\\');result+=c;}else{result.append(slashes,L'\\');result+=c;}slashes=0;}
    result.append(slashes*2,L'\\');return result+L"\"";
}
int launch(const fs::path &root,const std::vector<std::wstring> &args) {
    const auto executable=root/L"stel.exe";std::wstring command=quote(executable.wstring());
    for(const auto &arg:args)command+=L" "+quote(arg);
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    // GUI launches have no standard handles. CLI invocations may redirect them;
    // give the child inheritable duplicates so --version reaches the caller.
    std::array<HANDLE,3> inherited{};const DWORD standardIds[]={STD_INPUT_HANDLE,STD_OUTPUT_HANDLE,STD_ERROR_HANDLE};
    bool redirected=false;
    for(size_t n=0;n<inherited.size();++n) {
        const auto handle=GetStdHandle(standardIds[n]);
        if(handle&&handle!=INVALID_HANDLE_VALUE)
            DuplicateHandle(GetCurrentProcess(),handle,GetCurrentProcess(),&inherited[n],0,TRUE,DUPLICATE_SAME_ACCESS);
        redirected|=inherited[n]!=nullptr;
    }
    if(redirected){startup.dwFlags|=STARTF_USESTDHANDLES;startup.hStdInput=inherited[0];startup.hStdOutput=inherited[1];startup.hStdError=inherited[2];}
    const auto started=CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,redirected?TRUE:FALSE,0,nullptr,nullptr,&startup,&process);
    for(const auto handle:inherited)if(handle)CloseHandle(handle);
    if(!started)throw std::runtime_error("Cannot start Stel.");
    CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);DWORD code=1;GetExitCodeProcess(process.hProcess,&code);CloseHandle(process.hProcess);return int(code);
}
#else
volatile sig_atomic_t childPid=0;
void forwardSignal(int signal){if(childPid>0)kill(pid_t(childPid),signal);}
int launch(const fs::path &root,const std::vector<std::string> &args) {
    const auto executable=(root/"stel").string();std::vector<std::string> strings{executable};strings.insert(strings.end(),args.begin(),args.end());
    std::vector<char*> argv;for(auto &s:strings)argv.push_back(s.data());argv.push_back(nullptr);
    const auto pid=fork();if(pid<0)throw std::runtime_error("Cannot start Stel.");
    if(pid==0){execv(executable.c_str(),argv.data());_exit(127);}
    childPid=pid;for(int s:{SIGINT,SIGTERM,SIGHUP})signal(s,forwardSignal);
    int status=0;while(waitpid(pid,&status,0)<0)if(errno!=EINTR)throw std::runtime_error("Cannot wait for Stel.");
    childPid=0;return WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
}
#endif
template<class String> int run(const std::vector<String> &args) {
    const auto executable=self();std::ifstream file(executable,std::ios::binary);file.seekg(0,std::ios::end);const auto size=file.tellg();
    if(size<48)throw std::runtime_error("Missing Stel application payload.");
    file.seekg(size-std::streamoff(48));unsigned char footer[48];file.read(reinterpret_cast<char*>(footer),sizeof(footer));
    if(!file||std::memcmp(footer,magic,16))throw std::runtime_error("Missing Stel application payload.");
    const auto offset=integer(footer+16,8),length=integer(footer+24,8),format=integer(footer+32,8);
    if(format>1||offset>uint64_t(size)-48||length!=uint64_t(size)-48-offset)throw std::runtime_error("Damaged Stel container.");
    Temporary temporary;Reader reader(executable,offset,length,format==1);extract(reader,temporary.path);reader.finish();
    return launch(temporary.path,args);
}
}
#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    try{int count=0;auto **argv=CommandLineToArgvW(GetCommandLineW(),&count);if(!argv)throw std::runtime_error("Cannot read arguments.");std::vector<std::wstring> args;for(int n=1;n<count;++n)args.emplace_back(argv[n]);LocalFree(argv);return run(args);}
    catch(const std::exception &e){MessageBoxA(nullptr,e.what(),"Stel could not start",MB_OK|MB_ICONERROR);return 1;}
}
#else
int main(int argc,char **argv) {
    try{std::vector<std::string> args;for(int n=1;n<argc;++n)args.emplace_back(argv[n]);return run(args);}
    catch(const std::exception &e){std::cerr<<"Stel: "<<e.what()<<'\n';return 1;}
}
#endif
