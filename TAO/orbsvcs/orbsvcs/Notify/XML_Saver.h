/* -*- C++ -*- */

//=============================================================================
/**
 *  @file    XML_Saver.h
 *
 *  $Id$
 *
 *  @author Jonathan Pollack <pollack_j@ociweb.com>
 */
//=============================================================================

#ifndef XML_SAVER_H
#define XML_SAVER_H
#include /**/ "ace/pre.h"

#include "Topology_Saver.h"

#include "tao/corba.h"
#include "ace/streams.h"

#if !defined (ACE_LACKS_PRAGMA_ONCE)
#pragma once
#endif /* ACE_LACKS_PRAGMA_ONCE */

namespace TAO_Notify
{

/**
 * \brief Save Notification Service Topology to an XML file.
 */
class XML_Saver : public Topology_Saver
{
public:
  /// Construct an XML_Saver.
  /// Initialization is deferred to "open()"
  XML_Saver (bool timestamp = true);

  virtual ~XML_Saver ();

  /// Open the output file.
  /// \param file_name the fully qualified file name
  /// \return true if successful
  bool open (const ACE_CString & file_name, size_t backup_count);

  //////////////////////////////////
  // Override Topology_Saver methods
  // see Topology_Saver.h for doc
  virtual bool begin_object (CORBA::Long id,
    const ACE_CString& type,
    const NVPList& attrs,
    bool changed
    ACE_ENV_ARG_DECL);

  virtual void end_object (CORBA::Long id,
    const ACE_CString& type
    ACE_ENV_ARG_DECL);

  virtual void close (ACE_ENV_SINGLE_ARG_DECL);

private:
  void backup_file_name (char * file_path, int nfile);

  /// \return newstr to allow in-line use
  char* escape_string(char *& newstr, size_t & size, const ACE_CString & str);

private:
  /// A stream representing our current output.
  FILE * output_;
  bool close_out_;

  /// the name of the output file
  ACE_CString base_name_;
  size_t backup_count_;

  /// true to enable timestamping
  bool timestamp_;

  /// A string consisting of spaces that is our current indentation level.
  ACE_CString indent_;

};

} // namespace TAO_Notify

#include /**/ "ace/post.h"
#endif /* XML_SAVER_H */
