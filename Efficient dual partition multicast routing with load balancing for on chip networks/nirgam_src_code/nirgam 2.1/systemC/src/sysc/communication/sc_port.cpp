/*****************************************************************************

  The following code is derived, directly or indirectly, from the SystemC
  source code Copyright (c) 1996-2006 by all Contributors.
  All Rights reserved.

  The contents of this file are subject to the restrictions and limitations
  set forth in the SystemC Open Source License Version 2.4 (the "License");
  You may not use this file except in compliance with such restrictions and
  limitations. You may obtain instructions on how to receive a copy of the
  License at http://www.systemc.org/. Software distributed by Contributors
  under the License is distributed on an "AS IS" basis, WITHOUT WARRANTY OF
  ANY KIND, either express or implied. See the License for the specific
  language governing rights and limitations under the License.

 *****************************************************************************/

/*****************************************************************************

  sc_port.cpp -- Base classes of all port classes.

  Original Author: Martin Janssen, Synopsys, Inc., 2001-05-21

 *****************************************************************************/

/*****************************************************************************

  MODIFICATION LOG - modifiers, enter your name, affiliation, date and
  changes you are making here.

      Name, Affiliation, Date: Andy Goodrich, Forte Design Systems
                               Bishnupriya Bhattacharya, Cadence Design Systems,
                               25 August, 2003
  Description of Modification: phase callbacks

      Name, Affiliation, Date: Andy Goodrich, Forte Design Systems
	  						   12 December, 2005
  Description of Modification: multiport binding policy changes

 *****************************************************************************/


// $Log: sc_port.cpp,v $
// Revision 1.1.1.1  2006/12/15 20:31:35  acg
// SystemC 2.2
//
// Revision 1.9  2006/02/02 20:43:09  acg
//  Andy Goodrich: Added an existence linked list to sc_event_finder so that
//  the dynamically allocated instances can be freed after port binding
//  completes. This replaces the individual deletions in ~sc_bind_ef, as these
//  caused an exception if an sc_event_finder instance was used more than
//  once, due to a double freeing of the instance.
//
// Revision 1.7  2006/01/26 21:00:50  acg
//  Andy Goodrich: conversion to use sc_event::notify(SC_ZERO_TIME) instead of
//  sc_event::notify_delayed()
//
// Revision 1.6  2006/01/25 00:31:11  acg
//  Andy Goodrich: Changed over to use a standard message id of
//  SC_ID_IEEE_1666_DEPRECATION for all deprecation messages.
//
// Revision 1.5  2006/01/24 20:46:31  acg
// Andy Goodrich: changes to eliminate use of deprecated features. For instance,
// using notify(SC_ZERO_TIME) in place of notify_delayed().
//
// Revision 1.4  2006/01/13 20:41:59  acg
// Andy Goodrich: Changes to add port registration to the things that are
// checked when SC_NO_WRITE_CHECK is not defined.
//
// Revision 1.3  2006/01/13 18:47:42  acg
// Added $Log command so that CVS comments are reproduced in the source.
//

#include "sysc/kernel/sc_simcontext.h"
#include "sysc/kernel/sc_module.h"
#include "sysc/kernel/sc_method_process.h"
#include "sysc/kernel/sc_thread_process.h"
#include "sysc/communication/sc_communication_ids.h"
#include "sysc/utils/sc_utils_ids.h"
#include "sysc/communication/sc_event_finder.h"
#include "sysc/communication/sc_port.h"
#include "sysc/communication/sc_signal_ifs.h"

namespace sc_core
{

// ----------------------------------------------------------------------------
//  STRUCT : sc_bind_elem
// ----------------------------------------------------------------------------

struct sc_bind_elem
{
    // constructors
    sc_bind_elem();
    explicit sc_bind_elem( sc_interface* interface_ );
    explicit sc_bind_elem( sc_port_base* parent_ );

    sc_interface* iface;
    sc_port_base* parent;
};


// IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII

// constructors

sc_bind_elem::sc_bind_elem()
        : iface( 0 ),
        parent( 0 )
{}

sc_bind_elem::sc_bind_elem( sc_interface* interface_ )
        : iface( interface_ ),
        parent( 0 )
{}

sc_bind_elem::sc_bind_elem( sc_port_base* parent_ )
        : iface( 0 ),
        parent( parent_ )
{}


// ----------------------------------------------------------------------------
//  STRUCT : sc_bind_ef
// ----------------------------------------------------------------------------

struct sc_bind_ef
{
    // constructor
    sc_bind_ef( sc_process_b* , sc_event_finder* );

    // destructor
    ~sc_bind_ef();

    sc_process_b* handle;
    sc_event_finder* event_finder;
};


// IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII

// constructor

sc_bind_ef::sc_bind_ef( sc_process_b* handle_,
                        sc_event_finder* event_finder_ )
        : handle( handle_ ),
        event_finder( event_finder_ )
{}


// destructor

sc_bind_ef::~sc_bind_ef()
{
}


// ----------------------------------------------------------------------------
//  STRUCT : sc_bind_info
// ----------------------------------------------------------------------------

struct sc_bind_info
{
    // constructor
    explicit sc_bind_info( int max_size_,
                           sc_port_policy policy_=SC_ONE_OR_MORE_BOUND );

    // destructor
    ~sc_bind_info();

    int            max_size() const;
    sc_port_policy policy() const;
    int            size() const;

    int                        m_max_size;
    sc_port_policy             m_policy;
    std::vector<sc_bind_elem*> vec;
    bool                       has_parent;
    int                        last_add;
    bool                       is_leaf;
    bool                       complete;

    std::vector<sc_bind_ef*>   thread_vec;
    std::vector<sc_bind_ef*>   method_vec;
};


// IIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIII

// constructor

sc_bind_info::sc_bind_info( int max_size_, sc_port_policy policy_ )
        : m_max_size( max_size_ ),
        m_policy( policy_ ),
        has_parent( false ),
        last_add( -1 ),
        is_leaf( true ),
        complete( false )
{}


// destructor

sc_bind_info::~sc_bind_info()
{
    for ( int i = size() - 1; i >= 0; -- i )
    {
        delete vec[i];
    }
}


int
sc_bind_info::max_size() const
{
    return m_max_size ? m_max_size : vec.size();
}

sc_port_policy
sc_bind_info::policy() const
{
    return m_policy;
}

int
sc_bind_info::size() const
{
    return vec.size();
}



// ----------------------------------------------------------------------------
//  CLASS : sc_port_base
//
//  Abstract base class for class sc_port_b.
// ----------------------------------------------------------------------------

// This method exists to get around a problem in VCC 6.0 where you cannot
// have  a friend class that is templated. So sc_port_b<IF> calls this class
// instead of sc_process_b::add_static_event.

void sc_port_base::add_static_event(
    sc_method_handle process_p, const sc_event& event ) const
{
    process_p->add_static_event( event );
}

void sc_port_base::add_static_event(
    sc_thread_handle process_p, const sc_event& event ) const
{
    process_p->add_static_event( event );
}


// error reporting

void
sc_port_base::report_error( const char* id, const char* add_msg ) const
{
    char msg[BUFSIZ];
    if ( add_msg != 0 )
    {
        std::sprintf( msg, "%s: port '%s' (%s)", add_msg, name(), kind() );
    }
    else
    {
        std::sprintf( msg, "port '%s' (%s)", name(), kind() );
    }
    SC_REPORT_ERROR( id, msg );
}


// constructors

sc_port_base::sc_port_base(
    int max_size_, sc_port_policy policy
) :
        sc_object( sc_gen_unique_name( "port" ) ),
        m_bind_info( new sc_bind_info( max_size_, policy ) )
{
    simcontext()->get_port_registry()->insert( this );
}

sc_port_base::sc_port_base(
    const char* name_, int max_size_, sc_port_policy policy
) :
        sc_object( name_ ),
        m_bind_info( new sc_bind_info( max_size_, policy ) )
{
    simcontext()->get_port_registry()->insert( this );
}


// destructor

sc_port_base::~sc_port_base()
{
    simcontext()->get_port_registry()->remove( this );
    if ( m_bind_info != 0 )
    {
        delete m_bind_info;
    }
}


// bind interface to this port

void
sc_port_base::bind( sc_interface& interface_ )
{
    if ( m_bind_info == 0 )
    {
        // cannot bind an interface after elaboration
        report_error( SC_ID_BIND_IF_TO_PORT_, "simulation running" );
    }

    m_bind_info->vec.push_back( new sc_bind_elem( &interface_ ) );

    if ( ! m_bind_info->has_parent )
    {
        // add (cache) the interface
        add_interface( &interface_ );
        m_bind_info->last_add ++;
    }
}


// bind parent port to this port

void
sc_port_base::bind( this_type& parent_ )
{
    if ( m_bind_info == 0 )
    {
        // cannot bind a parent port after elaboration
        report_error( SC_ID_BIND_PORT_TO_PORT_, "simulation running" );
    }

    if ( &parent_ == this )
    {
        report_error( SC_ID_BIND_PORT_TO_PORT_, "same port" );
    }

    // check if parent port is already bound to this port
#if 0
    for ( int i = m_bind_info->size() - 1; i >= 0; -- i )
    {
        if ( &parent_ == m_bind_info->vec[i]->parent )
        {
            report_error( SC_ID_BIND_PORT_TO_PORT_, "already bound" );
        }
    }
#endif // 

    m_bind_info->vec.push_back( new sc_bind_elem( &parent_ ) );
    m_bind_info->has_parent = true;
    parent_.m_bind_info->is_leaf = false;
}

// called by sc_port_registry::construction_done (null by default)

void sc_port_base::before_end_of_elaboration()
{}

// called by elaboration_done (does nothing)

void
sc_port_base::end_of_elaboration()
{}

// called by sc_port_registry::start_simulation (does nothing by default)

void sc_port_base::start_of_simulation()
{}

// called by sc_port_registry::simulation_done (does nothing by default)

void sc_port_base::end_of_simulation()
{}


// called by class sc_module for positional binding

int
sc_port_base::pbind( sc_interface& interface_ )
{
    if ( m_bind_info == 0 )
    {
        // cannot bind an interface after elaboration
        report_error( SC_ID_BIND_IF_TO_PORT_, "simulation running" );
    }

    if ( m_bind_info->size() != 0 )
    {
        // first interface already bound
        return 1;
    }

    return vbind( interface_ );
}

int
sc_port_base::pbind( sc_port_base& parent_ )
{
    if ( m_bind_info == 0 )
    {
        // cannot bind a parent port after elaboration
        report_error( SC_ID_BIND_PORT_TO_PORT_, "simulation running" );
    }

    if ( m_bind_info->size() != 0 )
    {
        // first interface already bound
        return 1;
    }

    return vbind( parent_ );
}


// called by the sc_sensitive* classes

void
sc_port_base::make_sensitive( sc_thread_handle handle_,
                              sc_event_finder* event_finder_ ) const
{
    assert( m_bind_info != 0 );
    m_bind_info->thread_vec.push_back(
        new sc_bind_ef( (sc_process_b*)handle_, event_finder_ ) );
}

void
sc_port_base::make_sensitive( sc_method_handle handle_,
                              sc_event_finder* event_finder_ ) const
{
    assert( m_bind_info != 0 );
    m_bind_info->method_vec.push_back(
        new sc_bind_ef( (sc_process_b*)handle_, event_finder_ ) );
}


// support methods

int
sc_port_base::first_parent()
{
    for ( int i = 0; i < m_bind_info->size(); ++ i )
    {
        if ( m_bind_info->vec[i]->parent != 0 )
        {
            return i;
        }
    }
    return -1;
}

void
sc_port_base::insert_parent( int i )
{
    std::vector<sc_bind_elem*>& vec = m_bind_info->vec;

    this_type* parent = vec[i]->parent;


    // IF OUR PARENT HAS NO BINDING THEN IGNORE IT:
    //
    // Note that the zeroing of the parent pointer must occur before this
    // test

    vec[i]->parent = 0;
    if ( parent->m_bind_info->vec.size() == 0 ) return;

    vec[i]->iface = parent->m_bind_info->vec[0]->iface;
    int n = parent->m_bind_info->size() - 1;
    if ( n > 0 )
    {
        // resize the bind vector (by adding new elements)
        for ( int k = 0; k < n; ++ k )
        {
            vec.push_back( new sc_bind_elem() );
        }
        // move elements in the bind vector
        for ( int k = m_bind_info->size() - n - 1; k > i; -- k )
        {
            vec[k + n]->iface = vec[k]->iface;
            vec[k + n]->parent = vec[k]->parent;
        }
        // insert parent interfaces into the bind vector
        for ( int k = i + 1; k <= i + n; ++ k )
        {
            vec[k]->iface = parent->m_bind_info->vec[k - i]->iface;
            vec[k]->parent = 0;
        }
    }
}


// called when elaboration is done

void
sc_port_base::complete_binding()
{
    char msg_buffer[128]; // For error message construction.

    // IF BINDING HAS ALREADY BEEN COMPLETED IGNORE THIS CALL:

    assert( m_bind_info != 0 );
    if ( m_bind_info->complete )
    {
        return;
    }

    // COMPLETE BINDING OF OUR PARENT PORTS SO THAT WE CAN USE THAT INFORMATION:

    int i = first_parent();
    while ( i >= 0 )
    {
        m_bind_info->vec[i]->parent->complete_binding();
        insert_parent( i );
        i = first_parent();
    }

    // LOOP OVER BINDING INFORMATION TO COMPLETE THE BINDING PROCESS:

    int size;
    for ( int j = 0; j < m_bind_info->size(); ++ j )
    {
        sc_interface* iface = m_bind_info->vec[j]->iface;

        // if the interface is zero this was for an unbound port.
        if ( iface == 0 ) continue;

        // add (cache) the interface
        if ( j > m_bind_info->last_add )
        {
            add_interface( iface );
        }

        // only register "leaf" ports (ports without children)
        if ( m_bind_info->is_leaf )
        {
            iface->register_port( *this, if_typename() );
        }

        // complete static sensitivity for methods
        size = m_bind_info->method_vec.size();
        for ( int k = 0; k < size; ++ k )
        {
            sc_bind_ef* p = m_bind_info->method_vec[k];
            const sc_event& event = ( p->event_finder != 0 )
                                    ? p->event_finder->find_event(iface)
                                    : iface->default_event();
            p->handle->add_static_event( event );
        }

        // complete static sensitivity for threads
        size = m_bind_info->thread_vec.size();
        for ( int k = 0; k < size; ++ k )
        {
            sc_bind_ef* p = m_bind_info->thread_vec[k];
            const sc_event& event = ( p->event_finder != 0 )
                                    ? p->event_finder->find_event(iface)
                                    : iface->default_event();
            p->handle->add_static_event( event );
        }

    }

    // MAKE SURE THE PROPER NUMBER OF BINDINGS OCCURRED:
    //
    // Make sure there are enough bindings, and not too many.

    int actual_binds = interface_count();

    if ( actual_binds > m_bind_info->max_size() )
    {
        sprintf(msg_buffer, "%d binds exceeds maximum of %d allowed",
                actual_binds, m_bind_info->max_size() );
        report_error( SC_ID_COMPLETE_BINDING_, msg_buffer );
    }
    switch ( m_bind_info->policy() )
    {
    case SC_ONE_OR_MORE_BOUND:
        if ( actual_binds < 1 )
        {
            report_error( SC_ID_COMPLETE_BINDING_, "port not bound" );
        }
        break;
    case SC_ALL_BOUND:
        if ( actual_binds < m_bind_info->max_size() || actual_binds < 1 )
        {
            sprintf(msg_buffer, "%d actual binds is less than required %d",
                    actual_binds, m_bind_info->max_size() );
            report_error( SC_ID_COMPLETE_BINDING_, msg_buffer );
        }
        break;
    default:  // SC_ZERO_OR_MORE_BOUND:
        break;
    }


    // CLEAN UP: FREE BINDING STORAGE:

    size = m_bind_info->method_vec.size();
    for ( int k = 0; k < size; ++ k )
    {
        delete m_bind_info->method_vec[k];
    }
    m_bind_info->method_vec.resize(0);

    size = m_bind_info->thread_vec.size();
    for ( int k = 0; k < size; ++ k )
    {
        delete m_bind_info->thread_vec[k];
    }
    m_bind_info->thread_vec.resize(0);

    m_bind_info->complete = true;
}
void
sc_port_base::construction_done()
{
    before_end_of_elaboration();
}

void
sc_port_base::elaboration_done()
{
    assert( m_bind_info != 0 && m_bind_info->complete );
    delete m_bind_info;
    m_bind_info = 0;

    end_of_elaboration();
}

void
sc_port_base::start_simulation()
{
    start_of_simulation();
}

void
sc_port_base::simulation_done()
{
    end_of_simulation();
}


// ----------------------------------------------------------------------------
//  CLASS : sc_port_registry
//
//  Registry for all ports.
//  FOR INTERNAL USE ONLY!
// ----------------------------------------------------------------------------

void
sc_port_registry::insert( sc_port_base* port_ )
{
    if ( sc_is_running() )
    {
        port_->report_error( SC_ID_INSERT_PORT_, "simulation running" );
    }

#if defined(DEBUG_SYSTEMC)
    // check if port_ is already inserted
    for ( int i = size() - 1; i >= 0; -- i )
    {
        if ( port_ == m_port_vec[i] )
        {
            port_->report_error( SC_ID_INSERT_PORT_, "port already inserted" );
        }
    }
#endif

    // append the port to the current module's vector of ports
    sc_module* curr_module = m_simc->hierarchy_curr();
    if ( curr_module == 0 )
    {
        port_->report_error( SC_ID_PORT_OUTSIDE_MODULE_ );
    }
    curr_module->append_port( port_ );

    // insert
    m_port_vec.push_back( port_ );
}

void
sc_port_registry::remove( sc_port_base* port_ )
{
    int i;
    for ( i = size() - 1; i >= 0; -- i )
    {
        if ( port_ == m_port_vec[i] )
        {
            break;
        }
    }
    if ( i == -1 )
    {
        port_->report_error( SC_ID_REMOVE_PORT_, "port not registered" );
    }

    // remove
    m_port_vec[i] = m_port_vec[size() - 1];
    m_port_vec.resize(size()-1);
}


// constructor

sc_port_registry::sc_port_registry( sc_simcontext& simc_ )
        : m_simc( &simc_ )
{
}


// destructor

sc_port_registry::~sc_port_registry()
{
}

// called when construction is done

void
sc_port_registry::construction_done()
{
    for ( int i = size() - 1; i >= 0; -- i )
    {
        m_port_vec[i]->construction_done();
    }
}

// called when when elaboration is done

void
sc_port_registry::complete_binding()
{
    for ( int i = size() - 1; i >= 0; -- i )
    {
        m_port_vec[i]->complete_binding();
    }
}


// called when elaboration is done

void
sc_port_registry::elaboration_done()
{
    complete_binding();

    for ( int i = size() - 1; i >= 0; -- i )
    {
        m_port_vec[i]->elaboration_done();
    }
}

// called before simulation begins

void
sc_port_registry::start_simulation()
{
    for ( int i = size() - 1; i >= 0; -- i )
    {
        m_port_vec[i]->start_simulation();
    }
}

// called after simulation ends

void
sc_port_registry::simulation_done()
{
    for ( int i = size() - 1; i >= 0; -- i )
    {
        m_port_vec[i]->simulation_done();
    }
}

// This is a static member function.

void
sc_port_registry::replace_port( sc_port_registry* registry )
{
}

void sc_warn_port_constructor()
{
    static bool warn_port_constructor=true;
    if ( warn_port_constructor )
    {
        warn_port_constructor = false;
        SC_REPORT_INFO(SC_ID_IEEE_1666_DEPRECATION_,
                       "interface and/or port binding in port constructors is deprecated"
                      );
    }
}

} // namespace sc_core

// Taf!
