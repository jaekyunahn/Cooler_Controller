#include <pthread.h>
#include <stdio.h> 
#include <stdlib.h>
#include <string.h> 
#include <stdint.h>
#include <unistd.h>
#include <termios.h> 
#include <fcntl.h>    
#include <errno.h>
#include <time.h>
#include <signal.h>

#include <linux/fb.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/poll.h>

#define MAXLINE 256

volatile int global_keyboard_char = 0;


void fParsing(char * source , char start_char, char end_char, char *res, int sourcesize);

//string to int32
int fConvertToInt32(char *source, int sourcesize);

//string to float32
float fConvertStringToFloat32(char *source, int sourcesize);


void* pThread(void* data)
{
    pid_t pid; 
    pthread_t tid; 

    pid = getpid();
    tid = pthread_self();

	int ch = 0;
	
	while(1)
	{
		ch = getchar();
		if(ch!=0)
		{
			if(ch == 10)
			{
				global_keyboard_char = ch;
				break;
			}
		}
		ch = 0;
	}
	
	printf("thread abort\r\n");
}

int main(int argc, char **argv)
{
	float res = 0.0f;
	int ires = 0;
	
	char buffer[32] = "";

	//---------------------------------------------------------------------------------------------------------------------------------------------------------------------
	/*
	 *	pThread 생성 및 실행
	 */
	int thr_id;
	pthread_t p_thread;
	char p1[] = "keyboard_thread";
	
	thr_id = pthread_create(&p_thread, NULL, pThread, (void*)p1);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }

    pthread_detach(p_thread);
    pthread_join(p_thread, NULL);
	
	//---------------------------------------------------------------------------------------------------------------------------------------------------------------------
	/*
	 *	pipe를 이용한 쉘 명령어 실행
	 */
	FILE *fp;
    int state;

    char buff[MAXLINE];
    fp = popen("vcgencmd measure_temp", "r");
    if (fp == NULL)
    {
        perror("erro : ");
        exit(0);
    }
	
	//---------------------------------------------------------------------------------------------------------------------------------------------------------------------
	/* 
	 *	시리얼포트 통신 위한 초기화 코드
	 */
	int     fd, cnt;
	char    buf[128];

	struct termios    uart_io;  // http://neosrtos.com/docs/posix_api/termios.html
	struct pollfd     poll_fd;  // 체크할 event 정보를 갖는 struct

	unsigned char sSerialPortName[32] = "";
	int iSerialPortIndex = 0;

	//retry open Serial Port
	// 간혹 usb to uart를 작동중에 뽑으면 ttyUSB0이 아닌 ttyUSB1 이런식으로 증가한다
loop:
	memset(sSerialPortName, 0x00, sizeof(sSerialPortName));
	sprintf(sSerialPortName, "/dev/ttyUSB%d", iSerialPortIndex);

	// 시리얼 포트를 open
	fd = open(sSerialPortName, O_RDWR | O_NOCTTY);
	// 시리얼포트 open error
	if (0 > fd)
	{
		printf("open error:%s\n", sSerialPortName);
		// 시리얼 포트 번호가 65535 넘어가면
		if (iSerialPortIndex > 65535)
		{
			//종료
			return -1;
		}
		else
		{
			//count 올려서 시도
			iSerialPortIndex++;
			goto loop;
		}
	}

	// 시리얼 포트 통신 환경 설정
	memset(&uart_io, 0, sizeof(uart_io));
	/* control flags */
	uart_io.c_cflag = B115200 | CS8 | CLOCAL | CREAD;   //115200 , 8bit, 모뎀 라인 상태를 무시, 수신을 허용.
	/* output flags */
	uart_io.c_oflag = 0;
	/* input flags */
	uart_io.c_lflag = 0;
	/* control chars */
	uart_io.c_cc[VTIME] = 0;    //타이머의 시간을 설정
	uart_io.c_cc[VMIN] = 1;     //ead할 때 리턴되기 위한 최소의 문자 개수를 지정

	//라인 제어 함수 호출
	tcflush(fd, TCIFLUSH);              // TCIFLUSH : 입력을 비운다
	// IO 속성 선택
	tcsetattr(fd, TCSANOW, &uart_io);   // TCSANOW : 속성을 바로 반영
	// 시리얼 포트 디바이스 드라이버파일을 제어
	fcntl(fd, F_SETFL, FNDELAY);        // F_SETFL : 파일 상태와 접근 모드를 설정

	// poll 사용을 위한 준비   
	poll_fd.fd = fd;
	poll_fd.events = POLLIN | POLLERR;          // 수신된 자료가 있는지, 에러가 있는지
	poll_fd.revents = 0;

	//---------------------------------------------------------------------------------------------------------------------------------------------------------------------

	/*
	 *	Main Loop
	 */	
	while(1)
	{
		//-------------------------------------------------------------

		//라즈베리파이 CPU온도 읽기
		fp = popen("vcgencmd measure_temp", "r");
		if (fp == NULL)
		{
			perror("erro : ");
			exit(0);
		}
		
		//실행 값 가져오는 부분
		while (fgets(buff, MAXLINE, fp) != NULL);
		
		//필요없는 부분 제외한 데이터 가져오는 함수, 특정 문자와 문자 사이 가져오도록 
		//자작한 코드. 이런 형식의 고정된 포맷이라 딱히 고난이도는 필요 없다고 판단
		// 예시로 다음과 같이 뜨는데 temp=32.1'C
		fParsing(buff,0x3D,0x27,buffer,sizeof(buff));
		printf("%s\r\n",buffer);
		
		pclose(fp);
		
		//-------------------------------------------------------------

		// 파싱한 데이터를 float형태로 바꿔주고
		res = fConvertStringToFloat32(buffer,sizeof(buffer));	
		printf("res = %f\r\n",res);
		
		//소수점 1자리까지만 표현되는 온도 데이터를 10 곱하여 정수로 만든다
		ires = (int)(res * 10);
		printf("res = %d\r\n",ires);
		
		// 온도 데이터를 MCU로 전달 하기 위한 데이터 생성
		memset(buf,0x00,sizeof(buf));
		sprintf(buf,"[TEMP]%3d\r\n",ires);
		printf("[TEMP]%3d\r\n",ires);
		
		// 데이터 전송
		ires = write(fd, buf, strlen(buf));
		printf("ires=%d\r\n",ires);

		//1초 Delay
		sleep(1);

		//디버깅, MCU 에서 어떤 데이터 날라오는지 확인 하고 싶으면 활성화
#if 1
		cnt = read(fd, buf, 128);
		int y = 0;
        if (cnt > 0)
        {
			printf("incoming size=%d\r\n",cnt);
			printf("----------------------------------------------------------------------\r\n");
			printf("%s",buf);
            printf("\r\n----------------------------------------------------------------------\r\n");
		}
#endif
		//키보드 엔터키 누르는게 감지되면 종료
		if(global_keyboard_char == 10)
		{
			break;
		}
	}
	
	//End App
	printf("program exit\r\n");
	
	return 0;
}

//문자열 비교 함수
int fCompareFunction(char *source, char *target, int iSize)
{
	for(int i = 0 ; i < iSize ; i++ )
	{
		if(source[i] != target[i])
		{
			return -1;
		}
	}
	return 0;
}

//시작하는 문자와 끝나는 문자 사이 데이터 찾아 내는 함수
void fParsing(char * source , char start_char, char end_char, char *res, int sourcesize)
{
	int i, j;
	int resindex = 0;
	for( i = 0 ; i < sourcesize ; i++ )
	{
		if(source[i] == start_char )
		{
			i++;
			for( j = i ; j < sourcesize ; j++ )
			{
				if(source[j] == end_char)
				{
					return;
				}
				res[resindex] = source[j];
				resindex++;
			}
		}
	}
}

//string to int32
int fConvertToInt32(char *source, int sourcesize)
{
	int buf = source[0] - 0x30;
	int res = 0;
	
	int i;
	res = res + buf;
	
	for ( i = 1 ; i < sourcesize ; i++)
	{
		res = res * 10;
		buf = source[i] - 0x30;
		res = res + buf;
	}
	
	return res;
}

//string to float32
float fConvertStringToFloat32(char *source, int sourcesize)
{
	float res = 0.0f;
	float buf = 0.1f;
	int i, j , k;
	
	int iPointLocation = 0;
	
	//find "."
	for( i = 0 ; i < sourcesize ; i++ )
	{
		if( source[i] == '.')
		{
			iPointLocation = i;
			break;
		}
	}
	
	//float 가져오기
	buf = 0.1 * (source[iPointLocation + 1] - 0x30);
	res = buf;
	
#if 0
	for( i = iPointLocation + 2; i < sourcesize ; i++ )
	{
		//res = res / 10;
		//buf = 0.1f * (source[i] - 0x30);
		//res = res + buf;
		
	}
#endif
	//int level 가져오기
	res = res + fConvertToInt32(source ,iPointLocation);

	
	return res;
}


