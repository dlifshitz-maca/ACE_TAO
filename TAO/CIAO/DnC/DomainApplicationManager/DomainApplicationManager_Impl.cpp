// $Id$

#include "DomainApplicationManager_Impl.h"
#include "ace/Null_Mutex.h"
#include "ace/OS_NS_string.h"

#include "CIAO/DnC/Config_Handlers/DnC_Dump.h"

#if !defined (__ACE_INLINE__)
# include "DomainApplicationManager_Impl.inl"
#endif /* __ACE_INLINE__ */

CIAO::DomainApplicationManager_Impl::
DomainApplicationManager_Impl (CORBA::ORB_ptr orb,
                               PortableServer::POA_ptr poa,
                               Deployment::TargetManager_ptr manager,
                               const Deployment::DeploymentPlan & plan,
                               char * deployment_file)
  : orb_ (CORBA::ORB::_duplicate (orb)),
    poa_ (PortableServer::POA::_duplicate (poa)),
    target_manager_ (Deployment::TargetManager::_duplicate (manager)),
    plan_ (plan),
    deployment_file_ (CORBA::string_dup (deployment_file)),
    deployment_config_ (orb)
{
}

CIAO::DomainApplicationManager_Impl::~DomainApplicationManager_Impl ()
{
}

void
CIAO::DomainApplicationManager_Impl::
init (ACE_ENV_SINGLE_ARG_DECL_WITH_DEFAULTS)
  ACE_THROW_SPEC ((Deployment::ResourceNotAvailable,
                   Deployment::StartError,
                   Deployment::PlanError))
{
  ACE_TRY
    {
      // (1) Call get_plan_info() method to get the total number
      //     of child plans and list of NodeManager names, and
      // (2) Check the validity of the global deployment plan.
      if (! this->get_plan_info ())
        ACE_THROW (Deployment::PlanError ());

      // Call split_plan()
      if (! this->split_plan ())
        ACE_THROW (Deployment::PlanError ());

      // Invoke preparePlan for each child deployment plan.
      for (CORBA::ULong i = 0; i < this->num_child_plans_; i++)
        {
          // Get the NodeManager object reference.
          ::Deployment::NodeManager_var my_node_manager =
            this->deployment_config_.get_node_manager
              (this->node_manager_names_[i].c_str ());

          // Get the child deployment plan reference.
          ACE_Hash_Map_Entry
            <ACE_CString,
            Chained_Artifacts> *entry;

          if (this->artifact_map_.find (this->node_manager_names_[i],
                                        entry) != 0)
            ACE_THROW (Deployment::PlanError ());

          Chained_Artifacts artifacts = entry->int_id_;

          // Call preparePlan() method on the NodeManager with the
          // corresponding child plan as input, which returns a
          // NodeApplicationManager object reference.  @@TODO: Does
          // preparePlan take a _var type variable?

          ::Deployment::ApplicationManager_var app_manager =
            my_node_manager->preparePlan (artifacts.child_plan_
                                          ACE_ENV_ARG_PARAMETER);
          ACE_TRY_CHECK;

          // Narrow down to NodeApplicationManager object reference
          ::Deployment::NodeApplicationManager_var my_nam =
            ::Deployment::NodeApplicationManager::_narrow (app_manager.in ()
                                                           ACE_ENV_ARG_PARAMETER);
          ACE_TRY_CHECK;

          if (CORBA::is_nil (my_nam.in ()))
            {
              ACE_DEBUG ((LM_DEBUG, "DomainAppMgr::init () received a nil\
                                     reference for NodeApplicationManager\n"));
              ACE_THROW (Deployment::StartError ());
            }
          ACE_TRY_CHECK;

          // Cache the NodeApplicationManager object reference.
          artifacts.node_application_manager_ = my_nam._retn ();
        }
    }
  ACE_CATCHANY
    {
      ACE_PRINT_EXCEPTION (ACE_ANY_EXCEPTION,
                           "DomainApplicationManager_Impl::init\t\n");
      ACE_RE_THROW;
    }
  ACE_ENDTRY;
  ACE_CHECK_RETURN (0);
}

int
CIAO::DomainApplicationManager_Impl::
get_plan_info (void)
{
  CORBA::ULong length = this->plan_.instance.length ();

  // Error: If there are no nodes in the plan => No nodes to deploy the
  // components
  if (length == 0)
    return 0;

  // Copy the name of the node in the plan on to the node manager
  // array, Making sure that duplicates are not added twice
  int num_plans = 0;
  for (CORBA::ULong index = 0; index < length; index ++)
    {
      int matched = 0;
      for (CORBA::ULong i = 0; i < this->node_manager_names_.size (); i++)
        // If a match is found do not add it to the list of unique
        // node names
        if (ACE_OS::strcmp (this->plan_.instance [index].node.in (),
                            (this->node_manager_names_ [i]).c_str ()) == 0)
          {
            // Break out -- Duplicates found
            matched = 1;
            break;
          }

      if (! matched)
        {
          // Check if there is a corresponding NodeManager instance existing
          // If not present return false
          if (this->deployment_config_.get_node_manager
              (this->plan_.instance [index].node.in ()) == 0)
            return 0; /* Failure */

          // Add this unique node_name to the list of NodeManager names
          this->node_manager_names_.push_back
            (CORBA::string_dup
             (this->plan_.instance [index].node.in ()));

          // Increment the number of plans
          ++ num_plans;
        }
    }

  // Set the length of the Node Managers
  this->num_child_plans_ = num_plans;

  // Indicate success
  return 1;
}

//@@ We should ask those spec writers to look at the code below, hopefully
//   They will realize some thing.
int
CIAO::DomainApplicationManager_Impl::
split_plan (void)
{
  // Initialize the total number of child deployment plans specified
  // by the global plan.

 for (CORBA::ULong i = 0; i < this->num_child_plans_; i++)
  {
    ::Deployment::DeploymentPlan_var tmp_plan;
    ACE_NEW_RETURN (tmp_plan,
                    ::Deployment::DeploymentPlan,
                    0);

    tmp_plan->UUID = CORBA::string_dup (this->plan_.UUID.in ());

    tmp_plan->implementation.length (0);
    tmp_plan->instance.length (0);
    tmp_plan->connection.length (0);
    tmp_plan->externalProperty.length (0);
    tmp_plan->dependsOn.length (0);
    tmp_plan->artifact.length (0);
    tmp_plan->infoProperty.length (0);

    Chained_Artifacts artifacts;

    // Fill in the child_plan_ field, relinquishing ownership
    artifacts.child_plan_ = tmp_plan._retn ();

    // Fill in the node_manager_ field.
    artifacts.node_manager_ =
      this->deployment_config_.get_node_manager
              (this->node_manager_names_[i].c_str ());

    this->artifact_map_.bind (node_manager_names_[i], artifacts);
  }

   ACE_DEBUG ((LM_DEBUG, "after: initialize empty child plans...\n"));

  // (1) Iterate over the <instance> field of the global DeploymentPlan
  //     variabl.
  // (2) Retrieve the necessary information to contruct the node-level
  //     plans one by one.

  for (CORBA::ULong i = 0; i < (this->plan_.instance).length (); i++)
    {
      // Fill in the child deployment plan in the map.

      // Get the instance deployment description
      ::Deployment::InstanceDeploymentDescription my_instance =
        (this->plan_.instance)[i];

      // Find the corresponding child deployment plan entry in
      // the hash map for this instance.
      ACE_Hash_Map_Entry
        <ACE_CString,
        Chained_Artifacts> *entry;

      if (this->artifact_map_.find
          (ACE_CString (my_instance.node.in ()), //is this parameter correct?
	                                         //@@ Gan, now it should be correct.
                        entry) != 0)
        return 0;                          // no valid name found.

      // Get the child plan.
      ::Deployment::DeploymentPlan_var &child_plan =
          (entry->int_id_).child_plan_;

      // Fill in the contents of the child plan entry.

      // Append the "MonolithicDeploymentDescriptions implementation"
      // field with a new "implementation", which is specified by the
      // <implementationRef> field of <my_instance> entry.  NOTE: The
      // <artifactRef> field needs to be changed accordingly.
      ::Deployment::MonolithicDeploymentDescription my_implementation =
        (this->plan_.implementation)[my_instance.implementationRef];

      CORBA::ULong index_imp = child_plan->implementation.length ();
      child_plan->implementation.length (++index_imp);
      child_plan->implementation[index_imp-1] = my_implementation;

      // @@TODO: Create a ULong sequence of artifactRef, which will be
      // as the new artifactRef field for the implementation struct.

      // Initialize with the correct sequence length.
      CORBA::ULongSeq ulong_seq (my_implementation.artifactRef.length ());

      // Append the "ArtifactDeploymentDescriptions artifact" field
      // with some new "artifacts", which is specified by the
      // <artifactRef> sequence of <my_implementation> entry.
      for (CORBA::ULong iter = 0;
           iter < my_implementation.artifactRef.length ();
           iter ++)
        {
          CORBA::ULong artifact_ref = my_implementation.artifactRef[iter];

          CORBA::ULong index_art = child_plan->artifact.length ();
          child_plan->artifact.length (++index_art);
          child_plan->artifact[index_art-1] =
            (this->plan_.artifact)[artifact_ref];

          // @@ The artifactRef starts from 0.
          ulong_seq[iter] = index_art;
        }

      // Change the <artifactRef> field of the "implementation".
      child_plan->implementation[index_imp-1].artifactRef = ulong_seq;

      // Append the "InstanceDeploymentDescription instance" field with
      // a new "instance", which is almost the same as the "instance" in
      // the global plan except the <implementationRef> field.
      // NOTE: The <implementationRef> field needs to be changed accordingly.
      CORBA::ULong index_ins = child_plan->instance.length ();
      child_plan->instance.length (++index_ins);
      child_plan->instance[index_ins-1] = my_instance;

      // Change the <implementationRef> field of the "instance".
      // @@ The implementationRef starts from 0.
      child_plan->instance[index_ins-1].implementationRef = index_ins-1;
    }

  return 1;
}

void
CIAO::DomainApplicationManager_Impl::
add_connections (::Deployment::Connections & incoming_conn)
{
  CORBA::ULong old_len = this->all_connections_->length ();

  // Expand the length of the <all_connection_> sequence.
  this->all_connections_->length (old_len + incoming_conn.length ());

  // Store the connections to the <all_conections_> sequence
  for (CORBA::ULong i = 0; i < incoming_conn.length (); i++)
  {
    (*this->all_connections_)[old_len + i] = incoming_conn[i];
  }
}

bool
CIAO::DomainApplicationManager_Impl::
get_outgoing_connections (Deployment::Connections_out provided,
                          const Deployment::DeploymentPlan &plan)
{
  Deployment::Connections * retn_connections;
  ACE_NEW_RETURN (retn_connections,
		  Deployment::Connections,
		  0);

  Deployment::Connections_var safe = retn_connections;

  // For each component instance in the child plan ...
  for (CORBA::ULong i = 0; i < plan.instance.length (); i++)
  {
    // Get the component instance name
    if (!get_outgoing_connections_i (plan.instance[i].name.in (),
				     retn_connections))
      return 0;
  }
  provided = safe._retn (); // return the connection.
  return 1;
}



void
CIAO::DomainApplicationManager_Impl::
startLaunch (const ::Deployment::Properties & configProperty,
             ::CORBA::Boolean start
             ACE_ENV_ARG_DECL_WITH_DEFAULTS)
  ACE_THROW_SPEC ((CORBA::SystemException,
                   ::Deployment::ResourceNotAvailable,
                   ::Deployment::StartError,
                   ::Deployment::InvalidProperty))
{
  ACE_TRY
    {
      // Invoke startLaunch() operations on each cached NodeApplicationManager
      for (CORBA::ULong i = 0; i < this->num_child_plans_; i++)
        {
          // Get the NodeApplicationManager object reference.
          ACE_Hash_Map_Entry
            <ACE_CString,
            Chained_Artifacts> *entry;

          if (this->artifact_map_.find (this->node_manager_names_[i],
                                        entry) != 0)
            ACE_THROW (Deployment::StartError ()); // Should never happen!

          ::Deployment::NodeApplicationManager_var my_nam =
            (entry->int_id_).node_application_manager_.in ();

          ::Deployment::Connections_var retn_connections;

          // Obtained the returned NodeApplication object reference
          // and the returned Connections variable.
          ::Deployment::Application_var temp_application =
            my_nam->startLaunch (configProperty,
                                 retn_connections.out (),
                                 start);

          // Narrow down to NodeApplication object reference
          ::Deployment::NodeApplication_var my_na =
            ::Deployment::NodeApplication::_narrow (temp_application.in ()
                                                    ACE_ENV_ARG_PARAMETER);
          ACE_TRY_CHECK;

          // Cache the returned set of connections into the list.
          this->add_connections (retn_connections);

          // Cache the returned NodeApplication object reference into
          // the hash table.
          (entry->int_id_).node_application_ = my_na.in ();
        }
    }
  ACE_CATCHANY
    {
      ACE_PRINT_EXCEPTION (ACE_ANY_EXCEPTION,
                           "DomainApplicationManager_Impl::startLaunch\t\n");
      ACE_RE_THROW;
      return;
    }
  ACE_ENDTRY;

  ACE_CHECK_RETURN (0);
}


void
CIAO::DomainApplicationManager_Impl::
finishLaunch (::CORBA::Boolean start
              ACE_ENV_ARG_DECL_WITH_DEFAULTS)
  ACE_THROW_SPEC ((CORBA::SystemException,
                   Deployment::StartError))
{
  ACE_TRY
    {
      // Invoke finishLaunch() operation on each cached NodeApplication object.
      for (CORBA::ULong i = 0; i < this->num_child_plans_; i++)
        {
          // Get the NodeApplication object reference.
          ACE_Hash_Map_Entry
            <ACE_CString,
            Chained_Artifacts> *entry;

          if (this->artifact_map_.find (this->node_manager_names_[i],
                                        entry) != 0)
            ACE_THROW (Deployment::StartError ()); // Should never happen!

          Deployment::NodeApplication_var my_na =
            (entry->int_id_).node_application_.in ();

          // Get the Connections variable.
          Deployment::Connections_var my_connections;
          if (!this->get_outgoing_connections (my_connections.out (),
					      (entry->int_id_).child_plan_))
	    ACE_THROW (Deployment::StartError ());

          // Invoke finishLaunch() operation on NodeApplication.
          my_na->finishLaunch (my_connections,
                               start
                               ACE_ENV_ARG_PARAMETER);
          ACE_TRY_CHECK;
        }

    }
  ACE_CATCHANY
    {
      ACE_PRINT_EXCEPTION (ACE_ANY_EXCEPTION,
                           "DomainApplicationManager_Impl::finishLaunch\t\n");
      ACE_RE_THROW;
      return;
    }
  ACE_ENDTRY;

  ACE_CHECK_RETURN (0);
}


void
CIAO::DomainApplicationManager_Impl::
start (ACE_ENV_SINGLE_ARG_DECL_WITH_DEFAULTS)
  ACE_THROW_SPEC ((CORBA::SystemException,
                   ::Deployment::StartError))
{
  ACE_TRY
    {
      // Invoke start() operation on each cached NodeApplication object.
      for (CORBA::ULong i = 0; i < this->num_child_plans_; i++)
        {
          // Get the NodeApplication object reference.
          ACE_Hash_Map_Entry
            <ACE_CString,
            Chained_Artifacts> *entry;

          if (this->artifact_map_.find (this->node_manager_names_[i],
                                        entry) != 0)
            ACE_THROW (Deployment::StartError ()); // Should never happen!

          ::Deployment::NodeApplication_var my_na =
            (entry->int_id_).node_application_.in ();

          my_na->start (ACE_ENV_ARG_PARAMETER);
          ACE_TRY_CHECK;
        }
    }
  ACE_CATCHANY
    {
      ACE_PRINT_EXCEPTION (ACE_ANY_EXCEPTION,
                           "DomainApplicationManager_Impl::start\t\n");
      ACE_RE_THROW;
      return;
    }
  ACE_ENDTRY;

  ACE_CHECK_RETURN (0);
}

void
CIAO::DomainApplicationManager_Impl::
destroyApplication ()
  ACE_THROW_SPEC ((CORBA::SystemException,
                   ::Deployment::StopError))
{
  ACE_TRY
    {
      // Invoke destroyManager() operation on each cached
      // NodeManager object.
      for (CORBA::ULong i = 0; i < this->num_child_plans_; i++)
        {
          // Get the NodeManager and NodeApplicationManager object references.
          ACE_Hash_Map_Entry
            <ACE_CString,
            Chained_Artifacts> *entry;

          if (this->artifact_map_.find (this->node_manager_names_[i],
                                        entry) != 0)
            ACE_THROW (Deployment::StopError ()); // Should never happen!

          ::Deployment::NodeManager_var my_node_manager =
            (entry->int_id_).node_manager_;

          ::Deployment::NodeApplicationManager_var my_node_application_manager =
            (entry->int_id_).node_application_manager_.in ();

          // Invoke destoryManager() operation on the NodeManger.
          my_node_manager->destroyManager (my_node_application_manager.in ()
                                           ACE_ENV_ARG_PARAMETER);
          ACE_TRY_CHECK;
        }
    }
  ACE_CATCHANY
    {
      ACE_PRINT_EXCEPTION (ACE_ANY_EXCEPTION,
                           "DomainApplicationManager_Impl::destroyApplication\t\n");
      ACE_RE_THROW;
      return;
    }
  ACE_ENDTRY;

  ACE_CHECK_RETURN (0);
}


/// Returns the DeploymentPlan associated with this ApplicationManager.
::Deployment::DeploymentPlan *
CIAO::DomainApplicationManager_Impl::
getPlan (ACE_ENV_SINGLE_ARG_DECL_WITH_DEFAULTS)
  ACE_THROW_SPEC ((CORBA::SystemException))
{
  Deployment::DeploymentPlan_var plan = 0;
  // Make a deep copy of the Plan
  ACE_NEW_THROW_EX (plan,
                    Deployment::DeploymentPlan (this->plan_),
                    CORBA::NO_MEMORY ());

  // Transfer ownership
  return plan._retn ();
}

bool
CIAO::DomainApplicationManager_Impl::
get_outgoing_connections_i (const char * instname,
			    Deployment::Connections * retv)
{
  // Search in all the connections in the plan.
  for (CORBA::ULong i = 0; i < this->plan_.connection.length(); ++i)
  {
    CORBA::ULong len = retv->length ();

    // Current connection that we are looking at.
    const Deployment::PlanConnectionDescription & curr_conn
      = this->plan_.connection[i];

    //The modeling tool should make sure there are always 2 endpoints
    //in a connection.
    for (CORBA::ULong p_index = 0;
	 p_index < curr_conn.internalEndpoint.length ();
	 ++p_index)
    {
      const Deployment::PlanSubcomponentPortEndpoint & endpoint
	= curr_conn.internalEndpoint[p_index];

      // If the component name matches the name of one of the endpoints in the connection.
      if (ACE_OS::strcmp (this->plan_.instance[endpoint.instanceRef].name.in (),
			  instname) == 0 )
      {
	//Look at the port kind to make sure it's what we are interested in.
	if (endpoint.kind != Deployment::Facet &&
	    endpoint.kind != Deployment::EventConsumer)
	{
	  // The other endpoints in this connection is what we want.
	  CORBA::ULong index = (p_index +1)%2;

	  //Cache the name of the other component for later usage (search).
	  ACE_CString name =
	    this->plan_.instance[curr_conn.internalEndpoint[index].instanceRef].name.in ();

	  //Cache the name of the port from the other component for searching later.
	  ACE_CString port_name =
	    curr_conn.internalEndpoint[index].portName.in ();

	  bool found = false;
	  // Now we have to search in the received connections to get the objRef.
	  for (CORBA::ULong conn_index = 0;
	       conn_index < this->all_connections_->length ();
	       ++conn_index)
	  {
	    const Deployment::Connection curr_rev_conn = this->all_connections_[conn_index];

	    // We need to look at the instance name and the port name to confirm.
	    if (ACE_OS::strcmp (curr_rev_conn.instanceName.in (),
				name.c_str ()) == 0
		&&
		ACE_OS::strcmp (curr_rev_conn.portName.in (),
				port_name.c_str ()) == 0)
	    {
	      retv->length (len+1);
	      (*retv)[len].instanceName = instname;
	      (*retv)[len].portName = endpoint.portName.in ();
	      (*retv)[len].kind = endpoint.kind;
	      (*retv)[len].endpoint = curr_rev_conn.endpoint;  //No need to duplicate here.
	      ++len;             // This way we dont have do "-1" 4 times.
	      found = true;
	      break;             // Since we know there is only 2 endpoints in a connection.
	                         // so we dont have to worry about multiplex Receptacle etc.
	    }
	 }

	 // We didnt find the counter part connection even we are sure there must be 1.
	 if (!found ) return false;
	 break; // We know we have found the connection so even we are still on
	        // internalpoint 0 we can skip internalpoint 1.
	}
      }
    }  /* close for loop on internal endpoints */
  }  /* close for loop on all connections in the plan */
  return 1;
}
