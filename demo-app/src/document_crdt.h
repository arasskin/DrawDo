#pragma once
extern "C" {
#include "libyrs.h"
}
#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace document {
template<class F> inline void each_scalar(std::string_view text,F&& visit){
  for(size_t i=0;i<text.size();){auto c=uint8_t(text[i]);size_t n=c<128?1:(c>=0xc2&&c<=0xdf?2:(c>=0xe0&&c<=0xef?3:(c>=0xf0&&c<=0xf4?4:0)));
    if(!n||i+n>text.size())throw std::invalid_argument("Invalid UTF-8");
    uint32_t cp=c&((1u<<(7-n))-1);if(n==1)cp=c;
    for(size_t j=1;j<n;j++){auto b=uint8_t(text[i+j]);if((b&192)!=128)throw std::invalid_argument("Invalid UTF-8 continuation");cp=(cp<<6)|(b&63);}
    if((n==2&&cp<128)||(n==3&&cp<2048)||(n==4&&cp<65536)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))throw std::invalid_argument("Invalid UTF-8 scalar");
    visit(text.substr(i,n));i+=n;
  }
}
inline std::vector<std::string> scalars(std::string_view text){
  std::vector<std::string> out;
  each_scalar(text,[&](auto scalar){out.emplace_back(scalar);});return out;
}

// Thin ownership/editor adapter. Yrs implements the sequence, conflict resolution,
// tombstones, binary snapshots and merging. Offsets here are UTF-8 bytes.
class sequence {
  struct doc_delete { void operator()(YDoc* p) const { ydoc_destroy(p); } };
  struct transaction {
    YTransaction* value;
    ~transaction() { ytransaction_commit(value); }
    operator YTransaction*() const { return value; }
  };
  std::unique_ptr<YDoc,doc_delete> doc_;
  Branch* text_type_=nullptr;
  std::unique_ptr<YSubscription,decltype(&yunobserve)> updates_{nullptr,yunobserve};
  std::string text_;
  bool changed_=false;
  static void changed(void* context,uint32_t,const char*) {
    static_cast<sequence*>(context)->changed_=true;
  }
  void refresh() {
    transaction tx{ydoc_read_transaction(doc_.get())};
    auto length=ytext_len(text_type_,tx);
    char* raw=ytext_string(text_type_,tx);
    std::unique_ptr<char,decltype(&ystring_destroy)> owner(raw,ystring_destroy);
    text_.assign(raw,length);
  }
 public:
  sequence() { reset(); }
  void reset() {
    updates_.reset();doc_.reset();
    auto options=yoptions();options.encoding=Y_OFFSET_BYTES;
    doc_.reset(ydoc_new_with_options(options));text_type_=ytext(doc_.get(),"document");
    // A fresh client ID on every open/editor session avoids reusing IDs after
    // a crash loses an asynchronously flushed update. Identity/signing is separate.
    updates_.reset(ydoc_observe_updates_v2(doc_.get(),this,changed));
    text_.clear();changed_=false;
  }
  sequence(const sequence&)=delete;
  sequence& operator=(const sequence&)=delete;
  const std::string& text() const { return text_; }
  std::string state() const {
    transaction tx{ydoc_read_transaction(doc_.get())};
    uint32_t size=0;auto* raw=ytransaction_state_diff_v2(tx,nullptr,0,&size);
    std::string out(raw,size);ybinary_destroy(raw,size);return out;
  }
  bool merge(std::string_view update) {
    if(update.size()>UINT32_MAX)throw std::length_error("Document update too large");
    changed_=false;
    {
      transaction tx{ydoc_write_transaction(doc_.get(),0,nullptr)};
      if(ytransaction_apply_v2(tx,update.data(),uint32_t(update.size())))
        throw std::runtime_error("Invalid Yrs document update");
    }
    if(changed_)refresh();return changed_;
  }
  bool replace(const std::string& next) {
    if(next==text_)return false;
    if(next.size()>UINT32_MAX||next.find('\0')!=std::string::npos)
      throw std::invalid_argument("Document must be UTF-8 text without NUL characters");
    // Validate UTF-8 before entering the C API. Compare cached bytes, not a tree
    // lookup per character. Back up a shared partial code point at either edge.
    each_scalar(next,[](auto){});
    size_t prefix=0,suffix=0;
    while(prefix<std::min(next.size(),text_.size())&&next[prefix]==text_[prefix])++prefix;
    auto continuation=[](char c){return (uint8_t(c)&192)==128;};
    while(prefix<next.size()&&prefix&&continuation(next[prefix]))--prefix;
    while(suffix<next.size()-prefix&&suffix<text_.size()-prefix&&
          next[next.size()-1-suffix]==text_[text_.size()-1-suffix])++suffix;
    while(suffix&&continuation(next[next.size()-suffix]))--suffix;
    auto inserted=next.substr(prefix,next.size()-prefix-suffix);
    {
      transaction tx{ydoc_write_transaction(doc_.get(),0,nullptr)};
      auto removed=text_.size()-prefix-suffix;
      if(removed)ytext_remove_range(text_type_,tx,uint32_t(prefix),uint32_t(removed));
      if(!inserted.empty())ytext_insert(text_type_,tx,uint32_t(prefix),inserted.c_str(),nullptr);
    }
    text_=next;return true;
  }
  std::shared_ptr<sequence> fork() const {
    auto out=std::make_shared<sequence>();out->merge(state());return out;
  }
};
} // namespace document
