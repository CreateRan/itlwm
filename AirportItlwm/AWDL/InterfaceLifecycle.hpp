/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef AIRPORT_AWDL_INTERFACE_LIFECYCLE_HPP
#define AIRPORT_AWDL_INTERFACE_LIFECYCLE_HPP
namespace AirportAWDL {
// All transitions require the controller gate; this class does not lock.
class InterfaceLifecycle {
    bool accepting_ = false;
    bool creating_ = false;
    bool ownsChild_ = false;
public:
    bool open() {
        if (accepting_ || creating_ || ownsChild_) return false;
        accepting_ = true;
        return true;
    }
    bool accepting() const { return accepting_; }
    bool ownsChild() const { return ownsChild_; }
    bool beginCreate() {
        if (!accepting_ || creating_ || ownsChild_) return false;
        creating_ = true;
        return true;
    }
    bool completeCreate(bool attached) {
        if (!creating_) return false;
        creating_ = false;
        ownsChild_ = attached;
        return true;
    }
    void quiesce() { accepting_ = false; }
    void childDetached() { ownsChild_ = false; }
    bool canReleaseDependencies() const { return !accepting_ && !creating_ && !ownsChild_; }
};
}
#endif
