
# ExtIO\_RTLTCP.dll

Now you can use quality SDR software written for Windows on Linux with
your RTLSDR!  HDSDR runs well in WINE (the Windows software emulator) but 
all the ExtIO DLLs that directly access the USB port crash it.  The ExtIO DLL 
presented here, however, does not crash HDSDR in WINE since it uses networking.

## Discussion 

This repo contains a ExtIO DLL client implementation
for the rtl\_tcp RTL2832U SDR network server.
I have used it in HDSDR under WINE on Ubuntu.  

The DLL has been compiled for 32 bit 
Windows like HDSDR.  Initially HDSDR rejected
the DLL until I installed HDSDR using:

    WINEARCH=win32 wine HDSDR_install

The implementation does not connect to the rtl\_tcp
server until after the start hardware button is pressed in HDSDR. 
So the hostname and port number of the server can be configured
in the ExtIO GUI beforehand. 

At the beginning of the connection to the rtl\_tcp server,
all the other ExtIO GUI settings such as sample rate and gain
are next set through the server. 
Anything set in the ExtIO GUI after that does not take affect
until after the hardware is stopped and restarted.

Remember, when rtl\_tcp is run on a remote host,
rather than on 'localhost', it needs to be passed
its IP address or 0.0.0.0, otherwise
rtl\_tcp will not appear on the network.

    rtl_tcp -a 0.0.0.0

## Configuration Options

The ExtIO GUI does not provides a 'combo box' to select
values from for sample rate and other options.  

I recommend the following sample rate values to start.  

* 2400000
* 1800000
* 1200000
* 600000
* 300000

The other options are gain, frequency correction in hertz,
and direct sampling mode.

A blank value for an option (other than sample rate) 
will cause that configuration option not to be set 
when the connection with the rtl\_tcp server is started.

For the direct sampling GUI option, a 0 means disable
direct sampling, a one means use the I channel input,
and a two means use the Q channel.

## Issues

A sampling rate of 300000 seems to be too small for HDSDR,
causing it to chug.
The ExtIO DLL probably needs 
to send the samples to HDSDR faster when the RTLSDR is running 
at slower sampling rates.
A GUI option could be added to set this manually.  At
the moment this value is hardcoded.

I have had some issues with threading.  Sometimes 
when quitting HDSDR, WINE crashes giving a
page fault.  More testing or debugging is needed.
A new thread is created every time the start 
button pressed and a connection is made to the rtl\_tcp
server.  Any prior threads are terminated.  Maybe this
is done improperly.  Threading should probably be switched
to pthreads instead of win32.

I have not tested the ExtIO DLL under Windows yet.

## FYI

Reception is great using a netbook, all the interference lines that
dominate the waterfall on my desktop are gone on my netbook.

CubicSDR is an amazing piece of software.  It seems to find
extra sample bits somehow.  It runs on my
x86\_64 Ubuntu desktop.   It also compiles for Crouton under ARM, but the
display is slow since Crouton is unaccelerated.
However CubicSDR cannot use rtl\_tcp or any other network server yet.  

There is a rtl\_tcp like server for SDRPlay. I have not tried it.  However
the software has to down convert the Mirics chipset's samples from 12 bits to 8 bits to serve them.  It would be nice to provide a GUI option in this ExtIO DLL for 16 bit samples.

It would also be nice to know what other software
works under WINE using this ExtIO DLL.  
Does the SDR software Studio 1 work in WINE with it?
Unfortunately I know of no other SDR software that works under WINE
besides Spectravue (which does not support ExtIO DLLs) and HDSDR.


