#include "gl_driver.h"


#define PS1             0xC3
#define PS2             0x51
#define PS3             0xA1
#define PS4             0xF8
#define SM_SET          0
#define SM_GET          1
#define SM_STREAM       2
#define SM_ERROR        255
#define BI_PC2GL310     0x21
#define BI_GL3102PC     0x12
#define PE              0xC2


		
serial::Serial * port_;
uint8_t cs_;

//////////////////////////////////////////////////////////////
// Constructor and Deconstructor for GL Class
//////////////////////////////////////////////////////////////

Gl::Gl(std::string &port, uint32_t baudrate)
{
    OpenSerial(port, baudrate);
}

Gl::Gl()
{
    ;
}

Gl::~Gl()
{
    port_->close();
    delete port_;
}

void Gl::OpenSerial(std::string &port, uint32_t baudrate)
{
    port_ = new serial::Serial(port, baudrate, serial::Timeout::simpleTimeout(100));
    if(port_->isOpen()) std::cout << "GL Serial is opened." << std::endl;
}


//////////////////////////////////////////////////////////////
// Functions for Serial Comm
//////////////////////////////////////////////////////////////

void flush()
{
    port_->flush();
}

void cs_update(uint8_t data)
{
    cs_ = cs_^ (data&0xff);
}

uint8_t cs_get()
{
    return cs_&0xff;
}

void cs_clear()
{
    cs_ = 0;
}

void write(uint8_t data)
{
    port_->write(&data, 1);
    cs_update(data);
}

bool read(uint8_t *data)
{
    uint8_t buff;
    if (port_->read(&buff, 1) == 1)
    {
        *data = buff;
        cs_update(buff);
        return true;
    }
    else
    {
        return false;
    }
}

void write_PS()
{   
    std::vector<uint8_t> PS = {PS1, PS2, PS3, PS4};

    for(int i=0; i<PS.size(); i++) write(PS[i]);
}

void write_packet(uint8_t PI, uint8_t PL, uint8_t SM, uint8_t CAT0, uint8_t CAT1, std::vector<uint8_t> DTn)
{
    flush();
    cs_clear();

    write_PS();

    uint16_t DTL = DTn.size();

    uint16_t TL = DTL + 14;
    uint8_t buff = TL&0xff;
    write(buff);
    buff = (TL>>8)&0xff;
    write(buff);

    write(PI);
    write(PL);
    write(SM);
    write(BI_PC2GL310);
    write(CAT0);
    write(CAT1);

    for(int i=0; i<DTL; i++) write(DTn[i]);

    write(PE);

    write(cs_get());
}

bool check_PS(void)
{
    cs_clear();

    uint8_t data;
    while(read(&data)) 
    {
        if(data==PS1)
        {
            if(!read(&data)) return false;
            if(data==PS2)
            {
                if(!read(&data)) return false;
                if(data==PS3)
                {
                    if(!read(&data)) return false;
                    if(data==PS4)
                    {
                        return true;
                    }
                }    
            }
        }
        cs_clear();
    }
    
    return false;
}

std::vector<uint8_t> read_packet(uint8_t SM_check, uint8_t CAT0_check, uint8_t CAT1_check)
{
    std::vector<uint8_t> packet_data;

    while(check_PS())
    {
        packet_data.clear();

        uint8_t buff;
        if(!read(&buff)) return packet_data;
        uint16_t TL = buff&0xff;
        if(!read(&buff)) return packet_data;
        TL |= ((uint16_t)(buff&0xff))<<8;

        if(!read(&buff)) return packet_data;
        if(!read(&buff)) return packet_data;
           
        if(!read(&buff)) return packet_data;
        uint8_t SM = buff&0xff;
            
        if(!read(&buff) || (BI_GL3102PC!=(buff&0xff))) return packet_data;
            
        if(!read(&buff)) return packet_data;
        uint8_t CAT0 = buff&0xff;
        
        if(!read(&buff)) return packet_data;
        uint8_t CAT1 = buff&0xff;

        uint16_t DTL = TL - 14;
            
        for(int i=0; i<DTL; i++)
        {
            if(!read(&buff))
            {
                packet_data.clear();
                return packet_data;
            }
            packet_data.push_back(buff);
        }
            
        if(!read(&buff) || (PE!=(buff&0xff)))
        {
            packet_data.clear();
            return packet_data;
        } 

        uint8_t cs = cs_get();
        if(!read(&buff) || (cs!=(buff&0xff)))
        {
            packet_data.clear();
            return packet_data;
        }

        if((SM==SM_check) && (CAT0==CAT0_check) && (CAT1==CAT1_check)) return packet_data;
    }

    packet_data.clear();
    return packet_data;
}

//////////////////////////////////////////////////////////////
// Read GL Conditions
//////////////////////////////////////////////////////////////

std::string Gl::GetSerialNum(void)
{
    uint8_t PI = 0;
    uint8_t PL = 1;
    uint8_t SM = SM_GET;
    uint8_t CAT0 = 0x02;
    uint8_t CAT1 = 0x0A;

    std::vector<uint8_t> DTn = {1};
    write_packet(PI, PL, SM, CAT0, CAT1, DTn);

    for(int i=0; i<100; i++)
    {
        std::vector<uint8_t> packet_data = read_packet(SM, CAT0, CAT1);
        if(packet_data.size()>0)
        {
            std::string out_str(packet_data.begin(), packet_data.end());
            return out_str;
        }
    }
    
    std::string out_str = "[ERROR] Serial Number is not received.";
    return out_str;
}

Gl::framedata_t ParsingFrameData(std::vector<uint8_t> data)
{
    uint16_t frame_data_size = data[0]&0xff;
    frame_data_size |= ((uint16_t)(data[1]&0xff))<<8;

    Gl::framedata_t frame_data;
    frame_data.distance.resize(frame_data_size);
    frame_data.pulse_width.resize(frame_data_size);
    frame_data.angle.resize(frame_data_size);
    for(int i=0; i<frame_data_size; i++)
    {
        uint16_t distance = data[i*4+2]&0xff;
        distance |= ((uint16_t)(data[i*4+3]&0xff))<<8;

        uint16_t pulse_width = data[i*4+4]&0xff;
        pulse_width |= ((uint16_t)(data[i*4+5]&0xff))<<8;

        frame_data.distance[i] = distance/1000.0;
        frame_data.pulse_width[i] = pulse_width;
        frame_data.angle[i] = i*180.0/(frame_data_size-1)*3.141592/180.0;
    }

    return frame_data;
}

Gl::framedata_t Gl::ReadFrameData(void)
{
    Gl::framedata_t frame_data;

    uint8_t SM = SM_STREAM;
    uint8_t CAT0 = 0x01;
    uint8_t CAT1 = 0x02;

    std::vector<uint8_t> packet_data = read_packet(SM, CAT0, CAT1);
    if(packet_data.size()>0)
    {
        frame_data = ParsingFrameData(packet_data);
    }

    return frame_data;
}

//////////////////////////////////////////////////////////////
// Set GL Conditions
//////////////////////////////////////////////////////////////

void Gl::SetFrameDataEnable(uint8_t framedata_enable)
{
    uint8_t PI = 0;
    uint8_t PL = 1;
    uint8_t SM = SM_SET;
    uint8_t CAT0 = 0x1;
    uint8_t CAT1 = 0x3;

    std::vector<uint8_t> DTn = {framedata_enable};
    write_packet(PI, PL, SM, CAT0, CAT1, DTn);
}
