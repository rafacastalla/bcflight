#include <sys/fcntl.h>
#include "I2C.h"

I2C::I2C( int addr )
	: mAddr( addr )
{
}


I2C::~I2C()
{
}


int I2C::Read( uint8_t reg, void* buf, uint32_t len )
{
	// Depending on your hardware/software implementation, you may be able to do
	// only one I2C communication at a time, in this case a mutex should be
	// used to avoid any undefined behaviors (see boards/rpi/I2C.cpp implementation)
	return len;
}


int I2C::Write( uint8_t reg, void* buf, uint32_t len )
{
	return len;
}


int I2C::Read8( uint8_t reg, uint8_t* value )
{
	return 1;
}


int I2C::Read16( uint8_t reg, uint16_t* value )
{
	return 2;
}


int I2C::Read32( uint8_t reg, uint32_t* value )
{
	return 4;
}


int I2C::Write8( uint8_t reg, uint8_t value )
{
	return 1;
}


int I2C::Write16( uint8_t reg, uint16_t value )
{
	return 2;
}


int I2C::Write32( uint8_t reg, uint32_t value )
{
	return 4;
}


std::list< int > I2C::ScanAll()
{
	std::list< int > ret;
	// See boards/rpi/I2C.cpp implementation
	return ret;
}