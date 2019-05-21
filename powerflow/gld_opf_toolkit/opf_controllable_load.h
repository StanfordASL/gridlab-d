// $Id: opf_controllable_load.h$
//  Copyright (C) 2017 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)
#ifndef _OPF_CONTROLLABLE_LOAD_H
#define _OPF_CONTROLLABLE_LOAD_H


#include "gldcore/gridlabd.h"
#include "powerflow/load.h"

#include "opf_executive.h"

//Forward declare opf_executive due to cyclic dependency
class opf_executive;


class opf_controllable_load : public load {
public: // published variables
    // TODO add public typedefs
    // TODO declare published variables using GL_* macros

private: // unpublished variables
    // TODO add private typedefs
    // TODO add unpublished variables
    char executive_name[33];
    opf_executive *executive;


public: // required functions
    opf_controllable_load(MODULE *module);

    int create(void);

    int init(OBJECT *parent);

    // TODO add optional class functions
public: // optional/user-defined functions
    TIMESTAMP presync(TIMESTAMP t0);

    TIMESTAMP sync(TIMESTAMP t0);

    TIMESTAMP postsync(TIMESTAMP t0);


    // TODO add published class functions

private: // internal functions
    // TODO add desired internal functions

public: // required members
    static CLASS *oclass;
    static opf_controllable_load *defaults;
};


#endif //_OPF_CONTROLLABLE_LOAD_H
