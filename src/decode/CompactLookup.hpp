// Immutable DB2 lookup indexes without per-row heap nodes or copied path strings.
// Copyright (C) 2026 WarcraftXL. GPLv3.
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace wxl::db2::compact {
class IdIndex {
    std::vector<std::pair<int32_t, uint32_t>> rows_;
public:
    void Build(std::span<const int32_t> ids) {
        rows_.clear(); rows_.reserve(ids.size());
        for (uint32_t i=0; i<ids.size(); ++i) rows_.emplace_back(ids[i],i);
        // The old map assignment kept the LAST row for a duplicate ID.
        std::sort(rows_.begin(),rows_.end(),[](const auto& a,const auto& b){
            return a.first!=b.first ? a.first<b.first : a.second>b.second;
        });
        rows_.erase(std::unique(rows_.begin(),rows_.end(),[](const auto& a,const auto& b){
            return a.first==b.first;
        }),rows_.end());
    }
    bool Find(int32_t id,uint32_t& row) const {
        auto it=std::lower_bound(rows_.begin(),rows_.end(),id,[](const auto& a,int32_t b){return a.first<b;});
        if(it==rows_.end() || it->first!=id)return false;
        row=it->second;return true;
    }
    void Clear(){rows_.clear();}
    size_t Bytes() const {return rows_.capacity()*sizeof(rows_[0]);}
};

inline std::string_view Stem(std::string_view path) {
    const auto dot=path.find_last_of('.'), slash=path.find_last_of("/\\");
    if(dot!=path.npos && (slash==path.npos || dot>slash))path=path.substr(0,dot);
    return path;
}
inline unsigned char Fold(unsigned char c){return c=='/' ? '\\' : static_cast<unsigned char>(std::tolower(c));}
inline int Compare(std::string_view a,std::string_view b) {
    for(size_t i=0,n=std::min(a.size(),b.size());i<n;++i){
        const auto x=Fold(a[i]),y=Fold(b[i]);if(x!=y)return x<y?-1:1;
    }
    return a.size()==b.size()?0:a.size()<b.size()?-1:1;
}
class StemIndex {
    // Borrow only from immutable decoded tables which outlive this index.
    struct Entry {std::string_view stem;uint32_t id;};
    std::vector<Entry> rows_;
public:
    void Reserve(size_t count){rows_.reserve(count);}
    void Add(std::string_view path,uint32_t id){if(!path.empty())rows_.push_back({Stem(path),id});}
    void Finish(){
        // Stable order preserves FIRST spelling wins, including .mdx/.m2 aliases.
        std::stable_sort(rows_.begin(),rows_.end(),[](const auto& a,const auto& b){return Compare(a.stem,b.stem)<0;});
        rows_.erase(std::unique(rows_.begin(),rows_.end(),[](const auto& a,const auto& b){return Compare(a.stem,b.stem)==0;}),rows_.end());
    }
    uint32_t Find(std::string_view path) const {
        const auto key=Stem(path);
        auto it=std::lower_bound(rows_.begin(),rows_.end(),key,[](const auto& a,auto b){return Compare(a.stem,b)<0;});
        return it!=rows_.end() && Compare(it->stem,key)==0 ? it->id : 0;
    }
    size_t Size() const{return rows_.size();}
    size_t Bytes() const{return rows_.capacity()*sizeof(Entry);}
};

class MaterialIndex {
    struct Entry {uint32_t material,usage,id;};
    std::vector<Entry> rows_;
public:
    void Reserve(size_t count){rows_.reserve(count);}
    void Add(uint32_t material,uint32_t usage,uint32_t id){rows_.push_back({material,usage,id});}
    void Finish(){std::stable_sort(rows_.begin(),rows_.end(),[](const auto& a,const auto& b){return a.material<b.material;});}
    uint32_t Find(uint32_t material,uint32_t want) const {
        auto first=std::lower_bound(rows_.begin(),rows_.end(),material,[](const auto& a,uint32_t b){return a.material<b;});
        if(first==rows_.end() || first->material!=material)return 0;
        for(uint32_t target:{want,2u})
            for(auto it=first;it!=rows_.end() && it->material==material;++it)
                if(it->usage==target)return it->id;
        return first->id;
    }
    size_t Size() const{return rows_.size();}
    size_t Bytes() const{return rows_.capacity()*sizeof(Entry);}
};
}
