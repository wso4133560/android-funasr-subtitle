#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <chrono>
#include <deque>
#include <optional>
#include <string>
#include <vector>

struct Segment {
    uint64_t id=0;
    bool final=false;
    std::vector<float> audio;
    double start=0,end=0;
    std::chrono::steady_clock::time_point queued_at{};
};
// Input frames are exactly 10 ms. Partial revisions replace, never append to, a sentence.
class Segmenter {
    uint64_t id_=0;
    size_t clock_=0,start_=0, silence_=0,speech_=0,last_partial_=0;
    bool active_=false;
    std::deque<float> pre_;
    std::vector<float> audio_;
public:
    void reset(){active_=false;pre_.clear();audio_.clear();silence_=speech_=last_partial_=0;}
    std::optional<Segment> feed(const float* frame,float probability) {
        constexpr size_t frame_size=160,pre_size=3200,onset_size=480,max_audio=192000;
        clock_+=frame_size;
        double energy=0;for(size_t i=0;i<frame_size;++i)energy+=frame[i]*frame[i];
        // Digital silence / very low converter noise should never activate ASR.
        const bool voice=probability>=0.75f && energy/frame_size>1e-7;
        if(!active_){
            pre_.insert(pre_.end(),frame,frame+frame_size);
            while(pre_.size()>pre_size)pre_.pop_front();
            speech_=voice?speech_+frame_size:0;
            if(speech_<onset_size)return {};
            active_=true;++id_;audio_.assign(pre_.begin(),pre_.end());pre_.clear();
            start_=clock_-audio_.size();silence_=last_partial_=0;
        }else audio_.insert(audio_.end(),frame,frame+frame_size);
        silence_=voice?0:silence_+frame_size;
        const bool forced=audio_.size()>=max_audio;
        const bool final=silence_>=6400||forced;
        if(final){
            // Keep the only copy of a word that crosses a hard boundary.
            std::vector<float> carry;
            if(forced&&audio_.size()>pre_size){
                carry.assign(audio_.end()-pre_size,audio_.end());
                audio_.resize(audio_.size()-pre_size);
            }
            const double end=forced?double(start_+audio_.size())/16000.0:double(clock_-silence_)/16000.0;
            Segment out{id_,true,std::move(audio_),double(start_)/16000,end};
            reset();
            if(!carry.empty())pre_=std::deque<float>(carry.begin(),carry.end());
            return out;
        }
        if(audio_.size()>=16000 && audio_.size()-last_partial_>=16000){
            last_partial_=audio_.size();
            return Segment{id_,false,audio_,double(start_)/16000,double(clock_)/16000};
        }
        return {};
    }
    std::optional<Segment> flush(){
        if(!active_)return {};
        Segment out{id_,true,std::move(audio_),double(start_)/16000,double(clock_-silence_)/16000};reset();return out;
    }
};

// Latest partial wins; finals remain ordered. The caller serializes access.
class SegmentQueue {
    std::deque<Segment> finals_;
    std::optional<Segment> partial_;
public:
    size_t drops=0;
    void clear(){finals_.clear();partial_.reset();}
    void push(Segment s){
        if(s.final){
            if(partial_&&partial_->id<=s.id)partial_.reset();
            if(finals_.size()==4){finals_.pop_front();++drops;}
            finals_.push_back(std::move(s));
        }else partial_=std::move(s);
    }
    std::optional<Segment> pop(){
        if(!finals_.empty()){auto s=std::move(finals_.front());finals_.pop_front();return s;}
        auto p=std::move(partial_);partial_.reset();return p;
    }
    bool empty() const{return finals_.empty()&&!partial_;}
};
