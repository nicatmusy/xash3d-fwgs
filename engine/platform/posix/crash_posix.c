#include "common.h"

#define _XOPEN_SOURCE 1
#if XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX || XASH_APPLE
#ifndef XASH_OPENBSD
#include <ucontext.h>
#endif
#include <signal.h>
#include <sys/mman.h>
#include "library.h"
#include "input.h"
#include "crash.h"

static qboolean have_libbacktrace = false;

static void Sys_Crash( int signal, siginfo_t *si, void *context )
{
    char message[8192];
    int len, logfd;
    qboolean detailed_message = false;

    len = Q_snprintf( message, sizeof( message ),
        "Ver: " XASH_ENGINE_NAME " " XASH_VERSION " (build %i-%s-%s, %s-%s)\n",
        Q_buildnum(), g_buildcommit, g_buildbranch, Q_buildos(), Q_buildarch() );

#if !XASH_FREEBSD && !XASH_NETBSD && !XASH_OPENBSD && !XASH_APPLE
    len += Q_snprintf( message + len, sizeof( message ) - len,
        "Crash detected: signal %d errno %d with code %d at %p\n",
        signal, si->si_errno, si->si_code, si->si_addr );
#else
    len += Q_snprintf( message + len, sizeof( message ) - len,
        "Crash detected: signal %d errno %d with code %d at %p\n",
        signal, si->si_errno, si->si_code, si->si_addr );
#endif

    write( STDERR_FILENO, message, len );
    logfd = Sys_LogFileNo();
    write( logfd, message, len );

#if HAVE_LIBBACKTRACE
    if( have_libbacktrace && !detailed_message )
    {
        len = Sys_CrashDetailsLibbacktrace( logfd, message, len, sizeof( message ));
        detailed_message = true;
    }
#endif

#if HAVE_EXECINFO
    if( !detailed_message )
    {
        len = Sys_CrashDetailsExecinfo( logfd, message, len, sizeof( message ));
        detailed_message = true;
    }
#endif

#if !XASH_DEDICATED
    IN_SetMouseGrab( false );
#endif

    host.status = HOST_CRASHED;

    // 🟢 BURADA DEĞİŞİKLİK YAPTIK:
    // Çökme algılandığında doğrudan restart komutunu çalıştır.
    write( STDERR_FILENO, "[AutoRestart] Crash detected, restarting server...\n", 53 );
    Cbuf_AddText( "restart\n" );
    Cbuf_Execute();

    // Normal crash prosedürünü atla:
    return; // 🚫 Sys_Quit çağırma, sadece restart yap
}

static struct sigaction old_segv_act;
static struct sigaction old_abrt_act;
static struct sigaction old_bus_act;
static struct sigaction old_ill_act;

void Sys_SetupCrashHandler( const char *argv0 )
{
    struct sigaction act =
    {
        .sa_sigaction = Sys_Crash,
        .sa_flags = SA_SIGINFO | SA_ONSTACK,
    };

#if HAVE_LIBBACKTRACE
    have_libbacktrace = Sys_SetupLibbacktrace( argv0 );
#endif

    sigaction( SIGSEGV, &act, &old_segv_act );
    sigaction( SIGABRT, &act, &old_abrt_act );
    sigaction( SIGBUS,  &act, &old_bus_act );
    sigaction( SIGILL,  &act, &old_ill_act );
}

void Sys_RestoreCrashHandler( void )
{
    sigaction( SIGSEGV, &old_segv_act, NULL );
    sigaction( SIGABRT, &old_abrt_act, NULL );
    sigaction( SIGBUS,  &old_bus_act, NULL );
    sigaction( SIGILL,  &old_ill_act, NULL );
}

#endif // XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX
#endif // HAVE_LIBBACKTRACE

	sigaction( SIGSEGV, &act, &old_segv_act );
	sigaction( SIGABRT, &act, &old_abrt_act );
	sigaction( SIGBUS,  &act, &old_bus_act );
	sigaction( SIGILL,  &act, &old_ill_act );

}

void Sys_RestoreCrashHandler( void )
{
	sigaction( SIGSEGV, &old_segv_act, NULL );
	sigaction( SIGABRT, &old_abrt_act, NULL );
	sigaction( SIGBUS,  &old_bus_act, NULL );
	sigaction( SIGILL,  &old_ill_act, NULL );
}

#endif // XASH_FREEBSD || XASH_NETBSD || XASH_OPENBSD || XASH_ANDROID || XASH_LINUX
