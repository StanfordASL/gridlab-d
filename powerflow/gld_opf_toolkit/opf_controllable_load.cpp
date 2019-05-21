// $Id: opf_controllable_load.cpp$
//  Copyright (C) 2017 Alvaro Estandia
// Template valid as of Hassayampa (Version 3.0)

#include "opf_controllable_load.h"

EXPORT_CREATE(opf_controllable_load);
EXPORT_INIT(opf_controllable_load);
EXPORT_SYNC(opf_controllable_load);

// TODO add optional functions declarations
CLASS *opf_controllable_load::oclass = NULL;
opf_controllable_load *opf_controllable_load::defaults = NULL;

// TODO add declaration of class globals
opf_controllable_load::opf_controllable_load(MODULE *module) : load(module) {
    if (oclass != NULL) {
        exception("cannot register class more than once");
    }

    oclass = gld_class::create(module, "opf_controllable_load", sizeof(opf_controllable_load),
                               PC_FORCE_NAME | PC_PRETOPDOWN | PC_BOTTOMUP | PC_POSTTOPDOWN | PC_UNSAFE_OVERRIDE_OMIT |
                               PC_AUTOLOCK);
    if (oclass == NULL) {
        exception("opf_controllable_load registration failed");
    }

    //Technology readiness level is 0 for now
    oclass->trl = TRL_UNKNOWN;

    if (gl_publish_variable(oclass,
                            PT_INHERIT,
                            "node", //We don't publish the properties of load because they should not be edited from outside
                            PT_char32, "opf_executive", PADDR(executive_name), PT_DESCRIPTION,
                            "the opf_executive controlling this load",
                            NULL) < 1)
        throw "unable to publish opf_executive variables";

    // this should not be done if virtual functions are used
    memset(defaults = this, 0, sizeof(*this));
    // TODO set defaults
}

int opf_controllable_load::create(void) {
    memcpy(this, defaults, sizeof(*this));

    // TODO set defaults

    int res = load::create();
    return res;
}

int opf_controllable_load::init(OBJECT *parent) {
    int res = load::init(parent);

    if (has_phase(PHASE_D)) {
        GL_THROW("Delta connected opf_controllable_load is not supported.");
    }

    //This code is based on controller::init
    OBJECT *hdr = OBJECTHDR(this);
    char tname[32];
    char *namestr = (hdr->name ? hdr->name : tname);

    OBJECT *executive_obj = gl_get_object((char *) (&executive_name));
    if (executive_obj == NULL) {
        gl_error("%s: opf_controllable_load has no opf_executive.", namestr);
        return 0;
    }

    executive = OBJECTDATA(executive_obj, opf_executive);

    if ((executive_obj->flags & OF_INIT) != OF_INIT) {
        char objname[256];
        gl_verbose("opf_controllable_load::init(): deferring initialization on %s",
                   gl_name(executive_obj, objname, 255));
        return 2; // defer
    }

    gl_set_dependent(hdr, executive_obj);

    executive->RegisterControllableLoad(this);


    return res;
}
// TODO add implementations of optional class functions

TIMESTAMP opf_controllable_load::presync(TIMESTAMP t0) {
    TIMESTAMP result = load::presync(t0);
    return result;
}

TIMESTAMP opf_controllable_load::sync(TIMESTAMP t0) {
    TIMESTAMP result = load::sync(t0);
    return result;
}


TIMESTAMP opf_controllable_load::postsync(TIMESTAMP t0) {
    TIMESTAMP result = load::postsync(t0);
    return result;
}





