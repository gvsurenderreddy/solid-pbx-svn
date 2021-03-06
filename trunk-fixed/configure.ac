# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)

m4_define([PBX_VERSION],
          m4_bpatsubst(m4_esyscmd([build_tools/make_version .]),
                       [\([0-9.]*\)\(\w\|\W\)*],
                       [\1]))
AC_INIT(asterisk, PBX_VERSION, www.asterisk.org)

# cross-compile macros
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

# check existence of the package
AC_CONFIG_SRCDIR([asterisk.c])

# specify output header file
AC_CONFIG_HEADER(include/asterisk/autoconfig.h)

AC_COPYRIGHT("Asterisk")
AC_REVISION($Revision: 37105 $)

case "${host}" in
     *freebsd*)
     ac_default_prefix=/usr/local
     ;;
     *)
     ac_default_prefix=/usr
     if test ${sysconfdir} = '${prefix}/etc'; then
        sysconfdir=/etc
     fi
     if test ${mandir} = '${prefix}/man'; then
        mandir=/usr/share/man
     fi
     ;;
esac

if test ${localstatedir} = '${prefix}/var'; then
     localstatedir=/var
fi

### ** Platform.
AC_DEFINE_UNQUOTED(PBX_PLATFORM, "${host}",
[Define this to be the canonical name (cpu-vendor-os) of your system.])
AC_DEFINE_UNQUOTED(PBX_CPU, "${host_cpu}",
[Define this to be the name of the CPU of your system.])
AC_DEFINE_UNQUOTED(PBX_VENDOR, "${host_vendor}",
[Define this to be the name of the vendor of your system.])
AC_DEFINE_UNQUOTED(PBX_OS, "${host_os}",
[Define this to be the name of the OS of your system.])

# export some useful defines
PBX_PLATFORM=${host}
PBX_CPU=${host_cpu}
PBX_VENDOR=${host_vendor}
PBX_OS=${host_os}
AC_SUBST(PBX_PLATFORM)
AC_SUBST(PBX_CPU)
AC_SUBST(PBX_VENDOR)
AC_SUBST(PBX_OS)

#  check for uname
AC_PATH_TOOL([UNAME], [uname], No)
if test ! x"${UNAME}" = xNo; then
   PBX_OSREV=$(${UNAME} -r)
fi
AC_SUBST(PBX_OSREV)

# guest OS type
case "${host}" in
  cygwin*|mingw*|windows*|winnt)
    AC_DEFINE(Win32, 1,
              [Define according to your operating system type.])
    PBX_OSTYPE="CYGWIN"
# this is ugly - KPF
  OSISWIN32=1
  AC_SUBST(OSISWIN32)
    ;;
  *linux*)
    AC_DEFINE(Linux, 1,
              [Define according to your operating system type.])
    PBX_OSTYPE="Linux"
    ;;
  *netbsd*)
    AC_DEFINE(NetBSD, 1,
              [Define according to your operating system type.])
    PBX_OSTYPE="NetBSD"
    ;;
  *freebsd*)
    AC_DEFINE(FreeBSD, 1,
              [Define according to your operating system type.])
    PBX_OSTYPE="FreeBSD"
    # XXX temporary hack to let FreeBSD use the default paths
    # for local stuff. We need a better solution.
    if test x"${CPPFLAGS}" = x; then
        CPPFLAGS="-I/usr/local/include"
    fi
    if test x"${LDFLAGS}" = x; then
        LDFLAGS="-L/usr/local/lib"
    fi
    ;;
  *openbsd*)
    AC_DEFINE(OpenBSD, 1,
              [Define according to your operating system type.])
    PBX_OSTYPE="OpenBSD"
    ;;
  *sun*)
    AC_DEFINE(SunOS, 1,
              [Define according to your operating system type.])
    PBX_OSTYPE="SunOS"
    ;;
  *darwin*)
    AC_DEFINE(Darwin, 1,
              [Define according to your operating system type.])
    PBX_OSTYPE="Darwin"
    ;;
  *)
    AC_DEFINE(Unix, 1,
              [Define according to your operating system type.])
    PBX_OSTYPE="Unix"
   ;;
esac
AC_SUBST(PBX_OSTYPE)

# This needs to be before any macros that use the C compiler
AC_GNU_SOURCE

AH_TOP(
#ifndef ASTERISK_AUTOCONFIG_H
#define ASTERISK_AUTOCONFIG_H

#ifndef MENUSELECT
#include "asterisk/buildopts.h"
#endif

#ifndef _REENTRANT
#define _REENTRANT
#endif
)

AH_BOTTOM(
#endif
)

# cross-compile checks
if test x"${build}" != x"${host}"; 
then
   AC_CHECK_TOOL(CC, gcc, :)
   AC_CHECK_TOOL(CXX, g++, :)
   AC_CHECK_TOOL(RANLIB, ranlib, :)
   AC_CHECK_TOOL(AR, ar, :)

   if test x"${PBX_OSTYPE}" = xWin32;
   then
      AC_CHECK_TOOL(NM, nm, :)
      AC_CHECK_TOOL(WINDRES, windres, :)
      AC_CHECK_TOOL(DLLWRAP, dllwrap, :)
   fi
   crossCompile="Yes"
fi

# Checks for programs.
AC_PROG_CC
AC_PROG_CXX
AC_PROG_CPP
AC_PROG_CXXCPP
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_RANLIB
AST_CHECK_GNU_MAKE

AC_PATH_PROG([GREP], [grep], :)
AC_PATH_PROG([AR], [ar], :)
AC_PATH_PROG([FIND], [find], :)
AC_PATH_PROG([COMPRESS], [compress], :)
AC_PATH_PROG([BASENAME], [basename], :)
AC_PATH_PROG([DIRNAME], [dirname], :)
AC_PATH_PROG([SHELL], [sh], :)
AC_PATH_PROG([LN], [ln], :)
AC_PATH_PROG([DOT], [dot], :)

AC_LANG(C)

AC_ARG_ENABLE(dev-mode,
	[  --enable-dev-mode    Turn on developer mode],
	[case "${enableval}" in
	      y|ye|yes) AST_DEVMODE=yes ;;
	      n|no)  AST_DEVMODE=no ;;
	      *) AC_MSG_ERROR(bad value ${enableval} for --enable-dev-mode)  ;;
	esac])
AC_SUBST(AST_DEVMODE)

# package option names should be in alphabetical order
# by the --with option name, to make things easier for the users :-)

AST_EXT_LIB_SETUP([ALSA], [Advanced Linux Sound Architecture], [asound])
AST_EXT_LIB_SETUP([CURSES], [curses], [curses])
AST_EXT_LIB_SETUP([GSM], [GSM], [gsm], [, or 'internal'])
AST_EXT_LIB_SETUP([IKSEMEL], [Iksemel Jabber Library], [iksemel])
AST_EXT_LIB_SETUP([IMAP_TK], [UW IMAP Toolkit], [imap])
AST_EXT_LIB_SETUP([KDE], [KDE], [kde])
AST_EXT_LIB_SETUP([NBS], [Network Broadcast Sound], [nbs])
AST_EXT_LIB_SETUP([NCURSES], [ncurses], [ncurses])
AST_EXT_LIB_SETUP([NETSNMP], [Net-SNMP], [netsnmp])
AST_EXT_LIB_SETUP([NEWT], [newt], [newt])
AST_EXT_LIB_SETUP([UNIXODBC], [unixODBC], [odbc])
AST_EXT_LIB_SETUP([OGG], [OGG], [ogg])
AST_EXT_LIB_SETUP([OSPTK], [OSP Toolkit], [osptk])
AST_EXT_LIB_SETUP([OSS], [Open Sound System], [oss])
AST_EXT_LIB_SETUP([POPT], [popt], [popt])
AST_EXT_LIB_SETUP([PGSQL], [PostgreSQL], [postgres])
AST_EXT_LIB_SETUP([PRI], [ISDN PRI], [pri])
AST_EXT_LIB_SETUP([PWLIB], [PWlib], [pwlib])
AST_EXT_LIB_SETUP([QT], [Qt], [qt])
AST_EXT_LIB_SETUP([RADIUS], [Radius Client], [radius])
AST_EXT_LIB_SETUP([SPEEX], [Speex], [speex])
AST_EXT_LIB_SETUP([SQLITE], [SQLite], [sqlite])
AST_EXT_LIB_SETUP([OPENSSL], [OpenSSL], [ssl])
AST_EXT_LIB_SETUP([FREETDS], [FreeTDS], [tds])
AST_EXT_LIB_SETUP([TERMCAP], [Termcap], [termcap])
AST_EXT_LIB_SETUP([TINFO], [Term Info], [tinfo])
AST_EXT_LIB_SETUP([TONEZONE], [tonezone], [tonezone])
AST_EXT_LIB_SETUP([VORBIS], [Vorbis], [vorbis])
AST_EXT_LIB_SETUP([VPB], [Voicetronix API], [vpb])
AST_EXT_LIB_SETUP([ZLIB], [zlib], [z])
AST_EXT_LIB_SETUP([ZAPTEL], [Zaptel], [zaptel])

# check for basic system features and functionality before
# checking for package libraries

AC_FUNC_ALLOCA
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([arpa/inet.h fcntl.h inttypes.h libintl.h limits.h locale.h malloc.h netdb.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/file.h sys/ioctl.h sys/param.h sys/socket.h sys/time.h syslog.h termios.h unistd.h utime.h])

AC_SYS_LARGEFILE

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_UID_T
AC_C_INLINE
AC_TYPE_MODE_T
AC_TYPE_OFF_T
AC_TYPE_PID_T
AC_TYPE_SIZE_T
AC_CHECK_MEMBERS([struct stat.st_blksize])
AC_HEADER_TIME
AC_STRUCT_TM
AC_C_VOLATILE
AC_CHECK_TYPES([ptrdiff_t])

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_CLOSEDIR_VOID
AC_FUNC_ERROR_AT_LINE
AC_FUNC_FORK
AC_FUNC_FSEEKO
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_FUNC_MKTIME
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_FUNC_SELECT_ARGTYPES
AC_FUNC_SETVBUF_REVERSED
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRCOLL
AC_FUNC_STRFTIME
AC_FUNC_STRNLEN
AC_FUNC_STRTOD
AC_FUNC_UTIME_NULL
AC_FUNC_VPRINTF
AC_CHECK_FUNCS([atexit bzero dup2 endpwent floor ftruncate getcwd gethostbyname gethostname gettimeofday inet_ntoa isascii localtime_r memchr memmove memset mkdir munmap pow putenv re_comp regcomp rint select setenv socket sqrt strcasecmp strchr strcspn strdup strerror strncasecmp strndup strnlen strrchr strsep strspn strstr strtol unsetenv utime strtoq strcasestr asprintf vasprintf])

echo -n "checking for compiler atomic operations... "
AC_LINK_IFELSE(
AC_LANG_PROGRAM([], [int foo1; int foo2 = __sync_fetch_and_add(&foo1, 1);]),
AC_MSG_RESULT(yes)
AC_DEFINE([HAVE_GCC_ATOMICS], 1, [Define to 1 if your GCC C compiler provides atomic operations.]),
AC_MSG_RESULT(no)
)

AC_CHECK_HEADER([libkern/OSAtomic.h],
                [AC_DEFINE_UNQUOTED([HAVE_OSX_ATOMICS], 1, [Define to 1 if OSX atomic operations are supported.])])

AC_CHECK_SIZEOF(int)

# do the package library checks now

AST_EXT_LIB_CHECK([ALSA], [asound], [snd_spcm_init], [alsa/asoundlib.h], [-lm -ldl])

AST_EXT_LIB_CHECK([CURSES], [curses], [initscr], [curses.h])

GSM_INTERNAL="yes"
GSM_SYSTEM="yes"
if test "${USE_GSM}" != "no"; then
   if test "${GSM_DIR}" = "internal"; then
      GSM_SYSTEM="no"
   elif test "${GSM_DIR}" != ""; then
      GSM_INTERNAL="no"
   fi
   if test "${GSM_SYSTEM}" = "yes"; then
      gsmlibdir=""
      if test "x${GSM_DIR}" != "x"; then
      	 if test -d ${GSM_DIR}/lib; then
	    gsmlibdir="-L${GSM_DIR}/lib"
	 else
	    gsmlibdir="-L${GSM_DIR}"
	 fi
      fi
      AC_CHECK_LIB([gsm], [gsm_create], AC_DEFINE_UNQUOTED([HAVE_GSM], 1,
      [Define to indicate the GSM library]), [], ${gsmlibdir})
      if test "${ac_cv_lib_gsm_gsm_create}" = "yes"; then
	 GSM_LIB="-lgsm"
	 if test "x${GSM_DIR}" != "x"; then
	    GSM_LIB="${gsmlibdir} ${GSM_LIB}"
	    GSM_INCLUDE="-I${GSM_DIR}/include"
	 fi
	 PBX_GSM=1
	 GSM_INTERNAL="no"
      fi
   fi
   if test "${GSM_INTERNAL}" = "yes"; then
      GSM_LIB="internal"
      PBX_GSM=1
   fi
fi

AST_EXT_LIB_CHECK([IKSEMEL], [iksemel], [iks_start_sasl], [iksemel.h])

if test "${USE_IMAP_TK}" != "no"; then
   if test "${IMAP_TK_DIR}" = ""; then
      IMAP_TK_DIR=../imap-2004g
   fi
fi

AC_LANG_PUSH(C++)

if test "${USE_KDE}" != "no"; then
   echo -n "checking for crashHandler in -lkdecore... "
   saved_libs="${LIBS}"
   saved_cppflags="${CPPFLAGS}"
   CPPFLAGS="${CPPFLAGS} -I${KDE_DIR}/include"
   if test -d ${KDE_DIR}/lib; then
      kdelibdir="${KDE_DIR}/lib"
   else
      kdelibdir="${KDE_DIR}"
   fi
   LIBS="${LIBS} -L${kdelibdir} -lkdecore"

   AC_LINK_IFELSE(
	[AC_LANG_PROGRAM(
			[#include "kcrash.h"],
			[KCrash::defaultCrashHandler(1);])
	],
	[ac_cv_lib_kde_crash="yes"],
	[ac_cv_lib_kde_crash="no"])
		
   LIBS="${saved_libs}"
   CPPFLAGS="${saved_cppflags}"
	
   if test "${ac_cv_lib_kde_crash}" = "yes"; then
      AC_MSG_RESULT(yes) 
      KDE_LIB="-lkdecore -lkdeui"
      if test "${KDE_DIR}" != ""; then
      	 KDE_LIB="-L${kdelibdir} ${KDE_LIB}"
	 KDE_INCLUDE="-I${KDE_DIR}/include"
      fi
      PBX_KDE=1
      AC_DEFINE([HAVE_LIBKDE], 1, [Define if your system has the KDE libraries.])
   elif test ! -z "${KDE_MANDATORY}"; then
      AC_MSG_RESULT(no) 
      echo "***"
      echo "*** The KDE installation on this system appears to be broken."
      echo "*** Either correct the installation, or run configure"
      echo "*** including --without-kde."
      exit 1
   else
      AC_MSG_RESULT(no) 
   fi
fi
if test "${PBX_KDE}" = 1; then
   AC_PATH_TOOL(KDEINIT, kdeinit, No)
   if test ! x"${KDEINIT}" = xNo; then
      KDEDIR=$(${DIRNAME} ${KDEINIT})
      KDEDIR=$(${DIRNAME} ${KDEDIR})
   fi
   AC_SUBST([KDEDIR])
fi

AC_LANG_POP

AST_EXT_LIB_CHECK([NBS], [nbs], [nbs_connect], [nbs.h])

AST_EXT_LIB_CHECK([NCURSES], [ncurses], [initscr], [curses.h])

NETSNMP_CONFIG=No
if test "${USE_NETSNMP}" != "no"; then	
   if test "x${NETSNMP_DIR}" != "x"; then
      AC_PATH_TOOL([NETSNMP_CONFIG], [net-snmp-config], No, [${NETSNMP_DIR}/bin])
      if test x"${NETSNMP_CONFIG}" = xNo; then
         echo "***"
         echo "*** net-snmp-config was not found in the path you specified:"
         echo "*** ${NETSNMP_DIR}/bin"
         echo "*** Either correct the installation, or run configure"
         echo "*** including --without-netsnmp"
         exit 1
      fi
   else
      AC_PATH_TOOL([NETSNMP_CONFIG], [net-snmp-config], No)
   fi
fi
if test x"${NETSNMP_CONFIG}" != xNo; then
   NETSNMP_libs=`net-snmp-config --agent-libs`
   
   AC_CHECK_LIB([netsnmp], [snmp_register_callback], AC_DEFINE_UNQUOTED([HAVE_NETSNMP], 1,
   [Define to indicate the Net-SNMP library]), [], ${NETSNMP_libs})

   if test "${ac_cv_lib_netsnmp_snmp_register_callback}" = "yes"; then
      NETSNMP_LIB="${NETSNMP_libs}"
      PBX_NETSNMP=1
   elif test ! -z "${NETSNMP_MANDATORY}";
   then
      echo "***"
      echo "*** The Net-SNMP installation on this system appears to be broken."
      echo "*** Either correct the installation, or run configure"
      echo "*** including --without-netsnmp"
      exit 1
   fi
elif test ! -z "${NETSNMP_MANDATORY}";
then
   echo "***"
   echo "*** The Net-SNMP installation on this system appears to be broken."
   echo "*** Either correct the installation, or run configure"
   echo "*** including --without-netsnmp"
   exit 1
fi

AST_EXT_LIB_CHECK([NEWT], [newt], [newtBell], [newt.h])

AST_EXT_LIB_CHECK([UNIXODBC], [odbc], [SQLConnect], [sql.h])

AST_EXT_LIB_CHECK([OGG], [ogg], [ogg_sync_init], [])

AST_EXT_LIB_CHECK([OSPTK], [osptk], [OSPPCryptoDecrypt], [osp/osp.h], [-lcrypto -lssl])

PBX_OSS=0
AC_CHECK_HEADER([linux/soundcard.h],
	        [
		PBX_OSS=1
                AC_DEFINE_UNQUOTED([HAVE_OSS], 1, [Define to indicate the Open Sound System library])
		])
if test "$PBX_OSS" = "0"; then
   AC_CHECK_HEADER([sys/soundcard.h],
                   [
		   PBX_OSS=1
		   AC_DEFINE_UNQUOTED([HAVE_OSS], 1, [Define to indicate the Open Sound System library])
		   ])
fi
if test "$PBX_OSS" = "0"; then
   AST_EXT_LIB_CHECK([OSS], [ossaudio], [oss_ioctl_mixer], [soundcard.h])
fi

PG_CONFIG=No
if test "${USE_PGSQL}" != "no"; then	
   if test "x${PGSQL_DIR}" != "x"; then
      AC_PATH_TOOL([PG_CONFIG], [pg_config], No, [${PGSQL_DIR}/bin])
      if test x"${PG_CONFIG}" = xNo; then
         echo "***"
         echo "*** pg_config was not found in the path you specified:"
         echo "*** ${PGSQL_DIR}/bin"
         echo "*** Either correct the installation, or run configure"
         echo "*** including --without-postgres"
         exit 1
      fi
   else
      AC_PATH_TOOL([PG_CONFIG], [pg_config], No)
   fi
fi
if test "${PG_CONFIG}" != No; then
   PGSQL_libdir=`pg_config --libdir`
   PGSQL_includedir=`pg_config --includedir`
   
   AC_CHECK_LIB([pq], [PQexec], AC_DEFINE_UNQUOTED([HAVE_PGSQL], 1,
   [Define to indicate the PostgreSQL library]), [], -L${PGSQL_libdir} -lz)

   if test "${ac_cv_lib_pq_PQexec}" = "yes"; then
      PGSQL_LIB="-L${PGSQL_libdir} -lpq -lz"
      PGSQL_INCLUDE="-I${PGSQL_includedir}"
      PBX_PGSQL=1
   elif test ! -z "${PGSQL_MANDATORY}";
   then
      echo "***"
      echo "*** The PostgreSQL installation on this system appears to be broken."
      echo "*** Either correct the installation, or run configure"
      echo "*** including --without-postgres"
      exit 1
   fi
elif test ! -z "${PGSQL_MANDATORY}";
then
   echo "***"
   echo "*** The PostgreSQL installation on this system appears to be broken."
   echo "*** Either correct the installation, or run configure"
   echo "*** including --without-postgres"
   exit 1
fi

AST_EXT_LIB_CHECK([POPT], [popt], [poptStrerror], [popt.h])

AST_EXT_LIB_CHECK([PRI], [pri], [pri_call], [libpri.h])

PLATFORM_PTLIB="ptlib_${OSTYPE}_${MACHTYPE}_r"
if test "${USE_PWLIB}" != "no"; then
   echo -n "checking for existence of pwlib... "
	
   saved_cppflags="${CPPFLAGS}"
   saved_libs="${LIBS}"
   LIBS="${LIBS} -L${PWLIB_DIR} -l${PLATFORM_PTLIB}"
   CPPFLAGS="${CPPFLAGS} -I${PWLIB_DIR}/include"
	
   AC_LINK_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include "ptime.h"],
	[int q = PTime::IsDaylightSaving();])
	],
	[	AC_MSG_RESULT(yes) 
		ac_cv_lib_pwlib="yes" 
	],
	[	AC_MSG_RESULT(no) 
		ac_cv_lib_pwlib="no" 
	]
	)

   LIBS="${saved_libs}"
   CPPFLAGS="${saved_cppflags}"

   if test "${ac_cv_lib_pwlib}" = "yes"; then
      PWLIB_LIB="-l{PLATFORM_PWLIB}"
      if test "${PWLIB_DIR}" != ""; then
         PWLIB_LIB="-L${PWLIB_DIR}/lib ${PWLIB_LIB}"
         PWLIB_INCLUDE="-I${PWLIB_DIR}/include"
      fi
   	PBX_PWLIB=1
   	AC_DEFINE([HAVE_PWLIB], 1, [Define if your system has the pwlib libraries.])
   elif test ! -z "${PWLIB_MANDATORY}"; then
      echo "***"
      echo "*** The PWLIB installation on this system appears to be broken."
      echo "*** Either correct the installation, or run configure"
      echo "*** including --without-pwlib"
      exit 1
   fi
fi

AC_LANG_PUSH(C++)

if test "${USE_QT}" != "no"; then
   echo -n "checking for QDate in -lqt... "
   saved_libs="${LIBS}"
   saved_cppflags="${CPPFLAGS}"
   if test "x${QT_DIR}" != "x"; then
      LIBS="${LIBS} -L${QT_DIR}/lib"
      CPPFLAGS="${CPPFLAGS} -I${QT_DIR}/include"
   fi
   LIBS="${LIBS} -lqt"
   qtlib="qt"
   AC_LINK_IFELSE(
	[
		AC_LANG_PROGRAM(
		[#include <qt3/qdatetime.h>],
		[QDate date();])
	],
	[ac_cv_lib_qt_qt_date="yes"],
	[ac_cv_lib_qt_qt_date="no"])
   LIBS="${saved_libs}"
   CPPFLAGS="${saved_cppflags}"
	
   if test "${ac_cv_lib_qt_qt_date}" = "no"; then
      saved_libs="${LIBS}"
      saved_cppflags="${CPPFLAGS}"
      if test "x${QT_DIR}" != "x"; then
      	 LIBS="${LIBS} -L${QT_DIR}/lib"
	 CPPFLAGS="${CPPFLAGS} -I${QT_DIR}/include"
      fi
      LIBS="${LIBS} -lqt-mt"
      qtlib="qt-mt"	
      AC_LINK_IFELSE(
		[
			AC_LANG_PROGRAM(
			[#include <qt3/qdatetime.h>],
			[QDate date();])
		],
		[ac_cv_lib_qt_qt_date="yes"],
		[ac_cv_lib_qt_qt_date="no"])
      LIBS="${saved_libs}"
      CPPFLAGS="${saved_cppflags}"
   fi	

   if test "${ac_cv_lib_qt_qt_date}" = "yes"; then
      AC_MSG_RESULT(yes) 
   else
      AC_MSG_RESULT(no) 
   fi
	
   if test "${ac_cv_lib_qt_qt_date}" = "yes"; then
      QT_LIB="-l${qtlib}"
      if test "${QT_DIR}" != ""; then
      	 QT_LIB="-L${QT_DIR}/lib ${QT_LIB}"
	 QT_INCLUDE="-I${QT_DIR}/include"
      fi
      PBX_QT=1
      AC_DEFINE([HAVE_QT], 1, [Define if your system has the Qt library])
      AC_PATH_TOOL(QTMOC, moc, No)
   elif test ! -z "${QT_MANDATORY}"; 
   then
      echo "***"
      echo "*** The Qt installation on this system appears to be broken."
      echo "*** Either correct the installation, or run configure"
      echo "*** including --without-qt."
      exit 1
   fi
fi

AC_LANG_POP

AST_EXT_LIB_CHECK([RADIUS], [radiusclient-ng], [rc_read_config], [radiusclient-ng.h])

AST_EXT_LIB_CHECK([SPEEX], [speex], [speex_encode], [speex/speex.h], [-lm])

AST_EXT_LIB_CHECK([SQLITE], [sqlite], [sqlite_exec], [sqlite.h])

AST_EXT_LIB_CHECK([OPENSSL], [ssl], [ssl2_connect], [openssl/ssl.h], [-lcrypto])

AST_EXT_LIB_CHECK([FREETDS], [tds], [tds_version], [tds.h])
if test "${PBX_FREETDS}" != "0";
then
    case `grep TDS_VERSION_NO ${FREETDS_DIR:-/usr/include}/tdsver.h` in
    *0.63*)
        FREETDS_INCLUDE="${FREETDS_INCLUDE} -DFREETDS_0_63"
	;;
    *0.62*)
        FREETDS_INCLUDE="${FREETDS_INCLUDE} -DFREETDS_0_62"
	;;
    *)
        FREETDS_INCLUDE="${FREETDS_INCLUDE} -DFREETDS_PRE_0_62"
	;;
    esac
fi

AST_EXT_LIB_CHECK([TERMCAP], [termcap], [tgetent], [])

AST_EXT_LIB_CHECK([TINFO], [tinfo], [tgetent], [])

if test "${PBX_OSTYPE}" != "Linux" ; then
  tonezone_extra="-lm"
fi

AST_EXT_LIB_CHECK([TONEZONE], [tonezone], [tone_zone_find], [zaptel/tonezone.h], [${tonezone_extra}])

AST_EXT_LIB_CHECK([VORBIS], [vorbis], [vorbis_info_init], [vorbis/codec.h], [-lm -lvorbisenc])

AC_LANG_PUSH(C++)

if test "${USE_VPB}" != "no"; then
   echo -n "checking for vpb_open in -lvpb... "
   saved_libs="${LIBS}"
   saved_cppflags="${CPPFLAGS}"
   if test "x${VPB_DIR}" != "x"; then
      if test -d ${VPB_DIR}/lib; then
         vpblibdir=${VPB_DIR}/lib
      else
         vpblibdir=${VPB_DIR}
      fi
      LIBS="${LIBS} -L${vpblibdir}"
      CPPFLAGS="${CPPFLAGS} -I${VPB_DIR}/include"
   fi
   LIBS="${LIBS} -lvpb -lpthread"
   AC_LINK_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include <vpbapi.h>],
	[int q = vpb_open(0,0);])
	],
	[	AC_MSG_RESULT(yes) 
		ac_cv_lib_vpb_vpb_open="yes" 
	],
	[	AC_MSG_RESULT(no) 
		ac_cv_lib_vpb_vpb_open="no" 
	]
	)
   LIBS="${saved_libs}"
   CPPFLAGS="${saved_cppflags}"
   if test "${ac_cv_lib_vpb_vpb_open}" = "yes"; then
	VPB_LIB="-lvpb"
	if test "${VPB_DIR}" != ""; then
	   VPB_LIB="-L${vpblibdir}  ${VPB_LIB}"
	   VPB_INCLUDE="-I${VPB_DIR}/include"
	fi
	PBX_VPB=1
	AC_DEFINE([HAVE_VPB], 1, [Define if your system has the VoiceTronix API libraries.])
   elif test ! -z "${VPB_MANDATORY}"; then
      echo "***"
      echo "*** The VoiceTronix (vpb) installation on this system appears to be broken."
      echo "*** Either correct the installation, or run configure"
      echo "*** including --without-vpb."
      exit 1
   fi
fi

AC_LANG_POP

AST_EXT_LIB_CHECK([ZLIB], [z], [compress], [zlib.h])

if test "${USE_ZAPTEL}" != "no"; then
   echo -n "checking for zt_transcode_header in zaptel.h... "
   saved_cppflags="${CPPFLAGS}"
   if test "x${ZAPTEL_DIR}" != "x"; then
      CPPFLAGS="${CPPFLAGS} -I${ZAPTEL_DIR}/include"
   fi
   AC_COMPILE_IFELSE(
	[
	AC_LANG_PROGRAM(
	[#include <zaptel/zaptel.h>],
	[struct zt_transcode_header test;])
	],
	[	AC_MSG_RESULT(yes) 
		ac_cv_zaptel_h="yes" 
	],
	[	AC_MSG_RESULT(no) 
		ac_cv_zaptel_h="no" 
	]
	)
   CPPFLAGS="${saved_cppflags}"
   if test "${ac_cv_zaptel_h}" = "yes"; then
	if test "${ZAPTEL_DIR}" != ""; then
	   ZAPTEL_INCLUDE="-I${ZAPTEL_DIR}/include"
	fi
	PBX_ZAPTEL=1
	AC_DEFINE([HAVE_ZAPTEL], 1, [Define if your system has the Zaptel headers.])
   elif test ! -z "${ZAPTEL_MANDATORY}"; 
   then
      echo "***"
      echo "*** The Zaptel installation on this system appears to be broken."
      echo "*** Either correct the installation, or run configure"
      echo "*** including --without-zaptel."
      exit 1
   fi
fi

EDITLINE_LIB=""
if test "x$TERMCAP_LIB" != "x" ; then
  EDITLINE_LIB="$TERMCAP_LIB"
elif test "x$TINFO_LIB" != "x" ; then
  EDITLINE_LIB="$TINFO_LIB"
elif test "x$CURSES_LIB" != "x" ; then
  EDITLINE_LIB="$CURSES_LIB"
elif test "x$NCURSES_LIB" != "x" ; then
  EDITLINE_LIB="$NCURSES_LIB"
else
  echo "*** termcap support not found"
  exit 1
fi
AC_SUBST(EDITLINE_LIB)

AC_CHECK_HEADER([h323.h], [PBX_H323=1], [PBX_H323=0])
AC_SUBST(PBX_H323)

AC_CHECK_HEADER([linux/ixjuser.h], [PBX_IXJUSER=1], [PBX_IXJUSER=0], [
				   #include <linux/version.h>
				   #if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
				   #include <linux/compiler.h>
				   #endif
				   ])
AC_SUBST(PBX_IXJUSER)

PBX_GTK=0
AC_CHECK_TOOL(GTKCONFIG, gtk-config, No)
if test ! "x${GTKCONFIG}" = xNo; then
   GTK_INCLUDE=$(${GTKCONFIG} --cflags gthread)
   GTK_LIB=$(${GTKCONFIG} --libs gthread)
   PBX_GTK=1
   AC_DEFINE([HAVE_GTK], 1, [Define if your system has the GTK libraries.])
fi
AC_SUBST(PBX_GTK)
AC_SUBST(GTK_INCLUDE)
AC_SUBST(GTK_LIB)

PBX_CURL=0
AC_PATH_TOOL([CURL], [curl-config], No)
if test ! x"${CURL}" = xNo; then
# check for version
   CURLLIB=$(${CURL} --libs)
   PBX_CURL=1
   AC_DEFINE([HAVE_CURL], 1, [Define if your system has the curl libraries.])
fi
AC_SUBST(PBX_CURL)
AC_SUBST(CURLLIB)

AC_CONFIG_FILES([build_tools/menuselect-deps makeopts])
AC_OUTPUT

echo
echo "               .\$\$\$\$\$\$\$\$\$\$\$\$\$\$\$=..      "
echo "            .\$7\$7..          .7\$\$7:.    "
echo "          .\$\$:.                 ,\$7.7   "
echo "        .\$7.     7\$\$\$\$           .\$\$77  "
echo "     ..\$\$.       \$\$\$\$\$            .\$\$\$7 "
echo "    ..7\$   .?.   \$\$\$\$\$   .?.       7\$\$\$."
echo "   \$.\$.   .\$\$\$7. \$\$\$\$7 .7\$\$\$.      .\$\$\$."
echo " .777.   .\$\$\$\$\$\$77\$\$\$77\$\$\$\$\$7.      \$\$\$,"
echo " \$\$\$~      .7\$\$\$\$\$\$\$\$\$\$\$\$\$7.       .\$\$\$."
echo ".\$\$7          .7\$\$\$\$\$\$\$7:          ?\$\$\$."
echo "\$\$\$          ?7\$\$\$\$\$\$\$\$\$\$I        .\$\$\$7 "
echo "\$\$\$       .7\$\$\$\$\$\$\$\$\$\$\$\$\$\$\$\$      :\$\$\$. "
echo "\$\$\$       \$\$\$\$\$\$7\$\$\$\$\$\$\$\$\$\$\$\$    .\$\$\$.  "
echo "\$\$\$        \$\$\$   7\$\$\$7  .\$\$\$    .\$\$\$.   "
echo "\$\$\$\$             \$\$\$\$7         .\$\$\$.    "
echo "7\$\$\$7            7\$\$\$\$        7\$\$\$      "
echo " \$\$\$\$\$                        \$\$\$       "
echo "  \$\$\$\$7.                       \$\$  (TM)     "
echo "   \$\$\$\$\$\$\$.           .7\$\$\$\$\$\$  \$\$      "
echo "     \$\$\$\$\$\$\$\$\$\$\$\$7\$\$\$\$\$\$\$\$\$.\$\$\$\$\$\$      "
echo "       \$\$\$\$\$\$\$\$\$\$\$\$\$\$\$\$.                "
echo

echo "Package configured for: "
echo " OS type  : $PBX_OSTYPE"
echo " Host CPU : $host_cpu"
if test "x${crossCompile}" = xYes; then
   echo ""
   echo " Cross Compilation = YES"
   echo " Target            = ${host}"
fi
