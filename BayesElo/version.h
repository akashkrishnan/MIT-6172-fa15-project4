// Copyright (c) 2015 MIT License by 6.172 Staff

////////////////////////////////////////////////////////////////////////////
//
// version.h
//
// CVersion class declaration
//
// Remi Coulom
//
// May, 1997
//
////////////////////////////////////////////////////////////////////////////
#ifndef VERSION_H
#define VERSION_H

class CVersion {
private:  //////////////////////////////////////////////////////////////////
  static const char szVersion[];
  static const char szCopyright[];
  static const char szBinary[];

public:  ///////////////////////////////////////////////////////////////////
  static const char *GetVersion() {return szVersion;}
  static const char *GetCopyright() {return szCopyright;}
  static const char *GetBinaryCopyright() {return szBinary;}
};

#endif  // VERSION_H
