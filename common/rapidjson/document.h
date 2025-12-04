#pragma once

#include <string>
#include <utility>
#include <vector>

namespace rapidjson {

class Value {
public:
    Value() = default;
    explicit Value(int value) : int_value_(value), is_int_(true) {}

    bool IsInt() const { return is_int_; }
    int GetInt() const { return int_value_; }
    void SetInt(int value) {
        int_value_ = value;
        is_int_ = true;
    }

private:
    int int_value_{0};
    bool is_int_{false};
};

struct Member {
    std::string name;
    Value value;
};

class Document {
public:
    using MemberIterator = std::vector<Member>::iterator;
    using ConstMemberIterator = std::vector<Member>::const_iterator;

    MemberIterator FindMember(const char* key) {
        for (auto it = members_.begin(); it != members_.end(); ++it) {
            if (it->name == key) {
                return it;
            }
        }
        return members_.end();
    }

    ConstMemberIterator FindMember(const char* key) const {
        for (auto it = members_.cbegin(); it != members_.cend(); ++it) {
            if (it->name == key) {
                return it;
            }
        }
        return members_.cend();
    }

    MemberIterator MemberEnd() { return members_.end(); }
    ConstMemberIterator MemberEnd() const { return members_.cend(); }

    void AddMember(std::string key, const Value& value) {
        for (auto& member : members_) {
            if (member.name == key) {
                member.value = value;
                return;
            }
        }
        members_.push_back(Member{std::move(key), value});
    }

private:
    std::vector<Member> members_;
};

}  // namespace rapidjson

