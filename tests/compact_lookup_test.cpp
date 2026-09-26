#include "CompactLookup.hpp"
#include "Db2Decode.hpp"
#include "Wdc5.hpp"
#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <random>
#include <string>
#include <unordered_map>
#include <cstddef>

// Count requested live C++ allocation bytes, not process commit or allocator overhead.
static size_t liveBytes=0;
struct alignas(std::max_align_t) Allocation {size_t size;};
void* operator new(size_t n){auto p=static_cast<Allocation*>(std::malloc(sizeof(Allocation)+n));if(!p)throw std::bad_alloc();p->size=n;liveBytes+=n;return p+1;}
void operator delete(void* p) noexcept {if(p){auto h=static_cast<Allocation*>(p)-1;liveBytes-=h->size;std::free(h);}}
void operator delete(void* p,size_t) noexcept {::operator delete(p);}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,size_t) noexcept {::operator delete(p);}
namespace c=wxl::db2::compact;
std::string oldStem(const char* p){std::string s;for(;*p;++p)s+=*p=='/'?'\\':char(std::tolower(static_cast<unsigned char>(*p)));auto d=s.find_last_of('.'),sep=s.find_last_of('\\');if(d!=s.npos && (sep==s.npos || d>sep))s.resize(d);return s;}
void smallTests(){
 c::IdIndex ids;uint32_t row=99;ids.Build({});assert(!ids.Find(0,row));
 std::vector<int32_t> input={7,-1,7,INT32_MAX,0};ids.Build(input);assert(ids.Find(7,row)&&row==2);assert(ids.Find(-1,row)&&row==1);assert(!ids.Find(6,row));ids.Clear();assert(!ids.Find(7,row));
 c::StemIndex stems;stems.Add("CHARACTER/Human/Male.M2",10);stems.Add("character\\human\\male.mdx",20);stems.Add("a.b/no_extension",30);stems.Add("a/",40);stems.Finish();assert(stems.Find("character/human/MALE.mdx")==10);assert(stems.Find("a.b/no_extension")==30);assert(stems.Find("a/")==40);assert(!stems.Find("absent.m2"));
 c::MaterialIndex material;std::unordered_map<uint32_t,std::vector<std::pair<uint32_t,uint32_t>>> old;std::mt19937 rng(42);
 for(uint32_t i=1;i<=10000;++i){auto m=rng()%2000,u=rng()%5;material.Add(m,u,i);old[m].push_back({u,i});}material.Finish();
 for(uint32_t m=0;m<=2000;++m)for(uint32_t want=0;want<7;++want){uint32_t expected=0;auto it=old.find(m);if(it!=old.end()){for(uint32_t target:{want,2u}){for(auto [u,id]:it->second)if(u==target){expected=id;break;}if(expected)break;}if(!expected)expected=it->second[0].second;}assert(material.Find(m,want)==expected);}
}
void realTable(const char* file,bool models){
 std::ifstream f(file,std::ios::binary);assert(f);std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),{});
 wxl::features::db2::DB2Decoded table;assert(wxl::features::db2::DecodeDB2(raw.data(),uint32_t(raw.size()),table,nullptr,0));assert(table.rowSize==8);
 const size_t baseline=liveBytes;
 std::unordered_map<int32_t,uint32_t> oldIds;oldIds.reserve(table.ids.size());for(uint32_t i=0;i<table.ids.size();++i)oldIds[table.ids[i]]=i;
 const size_t oldIdBytes=liveBytes-baseline;
 c::IdIndex ids;ids.Build(table.ids);for(auto [id,expected]:oldIds){uint32_t row;assert(ids.Find(id,row)&&row==expected);}
 std::printf("%s rows=%zu old_id_bytes=%zu compact_id_bytes=%zu saved=%zu\n",models?"models":"textures",table.ids.size(),oldIdBytes,ids.Bytes(),oldIdBytes-ids.Bytes());
 if(models){
  const size_t stemBase=liveBytes;std::unordered_map<std::string,uint32_t> old;old.reserve(table.ids.size());
  auto pathAt=[&](uint32_t i){uint32_t off;std::memcpy(&off,table.records.data()+i*8+4,4);assert(off<table.strings.size());return table.strings.data()+off;};
  for(uint32_t i=0;i<table.ids.size();++i){auto p=pathAt(i);if(*p)old.emplace(oldStem(p),uint32_t(table.ids[i]));}
  const size_t oldBytes=liveBytes-stemBase;c::StemIndex compact;compact.Reserve(table.ids.size());for(uint32_t i=0;i<table.ids.size();++i)compact.Add(pathAt(i),uint32_t(table.ids[i]));compact.Finish();
  assert(compact.Size()==old.size());for(uint32_t i=0;i<table.ids.size();++i){auto p=pathAt(i);if(!*p)continue;auto key=oldStem(p);assert(compact.Find(p)==old.at(key));for(char& ch:key){if(ch=='\\')ch='/';else ch=char(std::toupper(static_cast<unsigned char>(ch)));}key+=".MDX";assert(compact.Find(key)==old.at(oldStem(p)));}
  std::printf("stems=%zu old_stem_bytes=%zu compact_stem_bytes=%zu saved=%zu\n",compact.Size(),oldBytes,compact.Bytes(),oldBytes-compact.Bytes());
 }
}
void realMaterials(const char* file){
 std::ifstream f(file,std::ios::binary);assert(f);std::vector<uint8_t> raw((std::istreambuf_iterator<char>(f)),{});
 using namespace wxl::runtime::db2::wdc5;
 const size_t baseline=liveBytes;Table table;std::string error;assert(table.Load(raw.data(),raw.size(),std::vector<FieldShape>(3),0xBD7C74C2u,&error));
 const size_t tableBytes=liveBytes-baseline;
 std::unordered_map<uint32_t,std::vector<std::pair<uint32_t,uint32_t>>> old;
 for(const auto& row:table.Rows())old[table.Value(row,2)].push_back({table.Value(row,1),row.id});
 const size_t oldBytes=liveBytes-baseline;
 c::MaterialIndex compact;compact.Reserve(table.Rows().size());for(const auto& row:table.Rows())compact.Add(table.Value(row,2),table.Value(row,1),row.id);compact.Finish();
 for(const auto& [m,values]:old)for(uint32_t want:{0u,1u,2u,3u,4u,0xffffffffu}){uint32_t expected=0;bool found=false;for(uint32_t target:{want,2u}){for(auto [u,id]:values)if(u==target){expected=id;found=true;break;}if(found)break;}if(!found)expected=values[0].second;assert(compact.Find(m,want)==expected);}
 std::printf("materials rows=%zu source_table_bytes=%zu old_table_and_index_bytes=%zu compact_bytes=%zu saved=%zu\n",table.Rows().size(),tableBytes,oldBytes,compact.Bytes(),oldBytes-compact.Bytes());
}
int main(int argc,char** argv){smallTests();if(argc>1){const std::string dir=argv[1];realTable((dir+"/ModelFilePath.db2").c_str(),true);realTable((dir+"/TextureFilePath.db2").c_str(),false);realMaterials((dir+"/texturefiledata.db2").c_str());}std::puts("PASS: compact lookup comparisons");}
