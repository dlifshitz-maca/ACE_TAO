// -*- C++ -*-
//
// $Id$

ACE_INLINE CORBA::ULong
TAO_LB_Location_Hash::operator() (
  const LoadBalancing::Location &location) const
{
  // @todo Interim implementation until we figure out what to do when a
  //       location has multiple components.

  CORBA::ULong len = location.length ();
  CORBA::ULong hash = 0;

  for (CORBA::ULong i = 0; i < len; ++i)
    hash +=
      ACE::hash_pjw (location[i].id.in ()) +
      ACE::hash_pjw (location[i].kind.in ());

  return hash;
}
