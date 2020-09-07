#include <CoreFoundation/CFRunLoop.h>
#include <CoreMIDI/MIDIServices.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

MIDIPortRef gOutPort = 0;
MIDIEndpointRef gDest = 0;
int gChannel = 0;

typedef unsigned char byte;

int read_exact(byte *buf, int len) {
    int i, got = 0;

    do {
	if ((i = read(0, buf + got, len - got)) <= 0) {
	    return (i);
	}
	got += i;
    } while (got < len);

    // dprintf(2, "read_exact len %d\n", len);
    return (len);
}

int write_exact(byte *buf, int len) {
    int i, wrote = 0;

    do {
	if ((i = write(1, buf + wrote, len - wrote)) <= 0) return (i);
	wrote += i;
    } while (wrote < len);

    return (len);
}

int read_cmd(byte *buf) {
    int len;

    if ((len = read_exact(buf, 2)) != 2) {
	// dprintf(2, "read_exact %d != 2\n", len);
	return (-1);
    }
    len = (buf[0] << 8) | buf[1];
    // dprintf(2, "read_cmd len %d\n", len);
    return read_exact(buf, len);
}

int write_cmd(byte *buf, int len) {
    byte li;

    li = (len >> 8) & 0xff;
    write_exact(&li, 1);

    li = len & 0xff;
    write_exact(&li, 1);

    return write_exact(buf, len);
}

void midiSend(byte *inBuf, int len) {
    MIDITimeStamp timestamp = 0;
    Byte buffer[1024];  // storage space for MIDI Packets
    MIDIPacketList *packetlist = (MIDIPacketList *)buffer;
    MIDIPacket *currentpacket = MIDIPacketListInit(packetlist);
    currentpacket = MIDIPacketListAdd(packetlist, sizeof(buffer), currentpacket,
				      timestamp, len, inBuf);
    OSStatus ret = MIDISend(gOutPort, gDest, packetlist);
    dprintf(2, "MIDISend OSSStatus: %d\n", ret);
}

static void readStdinEvent(CFFileDescriptorRef fdref,
			   CFOptionFlags callBackTypes, void *info) {
    // dprintf(2, "stdinEvent\n");
    byte buf[100];
    int len;
    if ((callBackTypes & kCFFileDescriptorReadCallBack)) {
	// dprintf(2, "readCallback\n");
	while ((len = read_cmd(buf)) > 0) {
	    dprintf(2, "read_cmd %d %d %d\n", buf[0], buf[1], buf[2]);
	    midiSend(buf, len);
	}
    } else {
	dprintf(2, "otherCallback\n");
	exit(1);
    }
}

static void MyReadProc(const MIDIPacketList *pktlist, void *refCon,
		       void *connRefCon) {
    if (gOutPort != 0 && gDest != 0) {
	MIDIPacket *packet = (MIDIPacket *)pktlist->packet;  // remove const (!)
	for (unsigned int j = 0; j < pktlist->numPackets; ++j) {
	    for (int i = 0; i < packet->length; ++i) {
		// dprintf(2, "%02X ", packet->data[i]);

		// rechannelize status bytes
		if (packet->data[i] >= 0x80 && packet->data[i] < 0xF0)
		    packet->data[i] = (packet->data[i] & 0xF0) | gChannel;
	    }

	    // dprintf(2, "\n");
	    write_cmd(packet->data, packet->length);
	    packet = MIDIPacketNext(packet);
	}

	// MIDISend(gOutPort, gDest, pktlist);
    }
}

int main(int argc, char *argv[]) {
    if (argc >= 2) {
	// first argument, if present, is the MIDI channel number to echo to
	// (1-16)
	sscanf(argv[1], "%d", &gChannel);
	if (gChannel < 1)
	    gChannel = 1;
	else if (gChannel > 16)
	    gChannel = 16;
	--gChannel;  // convert to 0-15
    }

    // create client and ports
    MIDIClientRef client = 0;
    MIDIClientCreate(CFSTR("MIDI Echo"), NULL, NULL, &client);

    MIDIPortRef inPort = 0;
    MIDIInputPortCreate(client, CFSTR("Input port"), MyReadProc, NULL, &inPort);
    MIDIOutputPortCreate(client, CFSTR("Output port"), &gOutPort);

    // enumerate devices (not really related to purpose of the echo program
    // but shows how to get information about devices)
    int i, n;
    CFStringRef pname, pmanuf, pmodel;
    char name[64], manuf[64], model[64];

    n = MIDIGetNumberOfDevices();
    for (i = 0; i < n; ++i) {
	MIDIDeviceRef dev = MIDIGetDevice(i);

	MIDIObjectGetStringProperty(dev, kMIDIPropertyName, &pname);
	MIDIObjectGetStringProperty(dev, kMIDIPropertyManufacturer, &pmanuf);
	MIDIObjectGetStringProperty(dev, kMIDIPropertyModel, &pmodel);

	CFStringGetCString(pname, name, sizeof(name), 0);
	CFStringGetCString(pmanuf, manuf, sizeof(manuf), 0);
	CFStringGetCString(pmodel, model, sizeof(model), 0);
	CFRelease(pname);
	CFRelease(pmanuf);
	CFRelease(pmodel);

	dprintf(2, "name=%s, manuf=%s, model=%s\n", name, manuf, model);
    }

    // open connections from all sources
    n = MIDIGetNumberOfSources();
    dprintf(2, "%d sources\n", n);
    for (i = 0; i < n; ++i) {
	MIDIEndpointRef src = MIDIGetSource(i);
	MIDIPortConnectSource(inPort, src, NULL);
    }

    // find the first destination
    n = MIDIGetNumberOfDestinations();
    if (n > 0) gDest = MIDIGetDestination(0);

    if (gDest != 0) {
	MIDIObjectGetStringProperty(gDest, kMIDIPropertyName, &pname);
	CFStringGetCString(pname, name, sizeof(name), 0);
	CFRelease(pname);
	dprintf(2, "Echoing to channel %d of %s\n", gChannel + 1, name);
    } else {
	dprintf(2, "No MIDI destinations present\n");
    }

    CFFileDescriptorRef fdref = CFFileDescriptorCreate(
	kCFAllocatorDefault, 0, true, readStdinEvent, NULL);
    CFFileDescriptorEnableCallBacks(fdref, kCFFileDescriptorReadCallBack);
    CFRunLoopSourceRef source =
	CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault, fdref, 0);
    CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
    CFRelease(source);

    CFRunLoopRun();
    dprintf(2, "runloop end... \n");

    return 0;
}
