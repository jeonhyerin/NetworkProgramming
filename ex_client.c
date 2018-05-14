#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/uio.h>
#include <mysql.h>


typedef struct
{
    char name[30];
    int userID;
        char date[30];
        char company[30];
    char writer[30];
        int book_num;
    int stat; // 도서 상태 식별. 0-> 도서관 내 소장 1-> 대여중 2-> 이 번호는 책이 없음.
}bookinfo;

//클라이언트에 가지고 있는 데이터 구조체
typedef struct {
    bookinfo book[1000];
}clientData;

//서버와의 데이터 전송을 위한 구조체
//Data 수정, 전송용
typedef struct {
    int dataInputA;
    char name1[30];
    char name2[30];
    char date1[32];
    char company1[40];
    char writer1[40];
    int sub_num;
}dataSubmit;

//사용자 세션 정보 저장 구조체
typedef struct {
    int session;
    int userID;
    int type;
}userSession;

#define MAXLINE 2048 //buf size
//client 요청 값
#define CLIID 0 //ID
#define CLIWRITE 1 //입력
#define CLIEDIT 2 //수정
#define CLIDELETE 3 //삭제
#define CLIEXIT 99 //exit
#define DB_USER "hyerin"
#define DB_PASS "me4023hy"
#define DB_NAME "library"

userSession session;
clientData cData;
struct iovec iov[3];
struct iovec iov1[3];

void noteClient(char *argv[]);
void createBook(char *str, time_t timer, int user);
void insertBook(char *str, int user);
void printBook();
void getMenu();

char buf[MAXLINE];
MYSQL mysql;
MYSQL_RES* res;
MYSQL_ROW row;

void noteClient(char *argv[]) 
{
    struct sockaddr_in servaddr;
    socklen_t str_len = sizeof(servaddr);
    int i, sockfd, choice=0, temp[3], temp1[3];
    int sessionConnect;
    char *aa;
    int Exit = 0;

    if((sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket fail");
        exit(0);
    }

    memset(&servaddr, 0, str_len);
    servaddr.sin_family = AF_INET;
    inet_pton(AF_INET, argv[1], &servaddr.sin_addr);
    servaddr.sin_port = htons(atoi(argv[2]));

    if(connect(sockfd, (struct sockaddr *)&servaddr, str_len) < 0) {
        perror("connect fail");
        exit(0);
    }

    printf("ID 입력 : ");
    fflush(stdin);
    fgets(buf, sizeof(buf), stdin);

    write(sockfd, &buf, sizeof(buf));
    read(sockfd, &session, sizeof(session));
    write(sockfd, "ok", 3);

    printf("Session No : %d, %d\n", session.session, session.userID);

    printf("%lu\n", sizeof(cData));
    recv(sockfd, &cData, sizeof(cData), MSG_WAITALL);
    
    getMenu();

    while(1) 
    {
        buf[0] = '\0';
        
        puts("메뉴 보기는 100");
        printf("메뉴 선택 : ");
        fflush(stdin);
        scanf("%d", &session.type);

        write(sockfd, &session, sizeof(session));
        read(sockfd, &sessionConnect, sizeof(sessionConnect));
        printf("%d\n", sessionConnect);
        if(sessionConnect == -1) {
            printf("Error : Connect Session Error....\n");
            close(sockfd);
            break;
        }

        dataSubmit dSubmit;
        switch(session.type) {
            case CLIWRITE:
                puts("도서 입력");
                printf("도서 선택 : ");
                fflush(stdin);
                scanf("%d", &dSubmit.dataInputA);
            
                printf("도서 이름 : ");
                fflush(stdin);
                scanf("%s", dSubmit.name1);
                temp[0] = strlen(dSubmit.name1);
                write(sockfd, &temp[0], sizeof(temp[0]));
                read(sockfd, NULL, 0);
    
                printf("책 작가 입력\n");
                fflush(stdin);
                        scanf("%s",dSubmit.writer1);
                temp[1] = strlen(dSubmit.writer1);
                write(sockfd, &temp[1], sizeof(temp[1]));
                read(sockfd, NULL, 0);

                        printf("책 회사 입력\n");
                fflush(stdin);
                        scanf("%s",dSubmit.company1);
                temp[2] = strlen(dSubmit.company1);
                write(sockfd, &temp[2], sizeof(temp[2]));
                read(sockfd, NULL, 0);

                iov[0].iov_base = dSubmit.name1;
                iov[0].iov_len = temp[0];

                iov[1].iov_base = dSubmit.writer1;
                iov[1].iov_len = temp[1];

                iov[2].iov_base = dSubmit.company1;
                iov[2].iov_len = temp[2];

                writev(sockfd, iov, 3);
                // write(sockfd, &dSubmit, sizeof(dSubmit));

                break;

            case CLIEDIT:
                puts("도서 수정");
                printBook();
                printf("수정할 도서이름을 입력하세요 : ");
                fflush(stdin);
                scanf("%s", dSubmit.name1);
                write(sockfd, &dSubmit, sizeof(dSubmit));

                printf("바꿀 도서 이름 \n");
                fflush(stdin);
                scanf("%s", dSubmit.name2);
                temp1[0] = strlen(dSubmit.name2);
                write(sockfd, &temp1[0], sizeof(temp1[0]));
                read(sockfd, NULL, 0);

                printf("바꿀 도서의 작가 수정 \n");
                fflush(stdin);
                scanf("%s", dSubmit.writer1);
                temp1[1] = strlen(dSubmit.writer1);
                write(sockfd, &temp1[1], sizeof(temp1[1]));
                read(sockfd, NULL, 0);

                printf("바꿀 도서의 출판사 수정 \n");
                fflush(stdin);
                scanf("%s", dSubmit.company1);
                temp1[2] = strlen(dSubmit.company1);
                write(sockfd, &temp1[2], sizeof(temp1[2]));
                read(sockfd, NULL, 0);

                
                iov1[0].iov_base = dSubmit.name2;
                iov1[0].iov_len = temp1[0];

                iov1[1].iov_base = dSubmit.writer1;
                iov1[1].iov_len = temp1[1];

                iov1[2].iov_base = dSubmit.company1;
                iov1[2].iov_len = temp1[2];
            
                writev(sockfd, iov1, 3);
                
                break;

            case CLIDELETE:
                puts("도서 삭제");
                printf("도서 이름 입력 : ");
                fflush(stdin);
                scanf("%s", dSubmit.name1);                
        
                write(sockfd, &dSubmit, sizeof(dSubmit));
                break;


            case CLIEXIT:
                read(sockfd, &buf, sizeof(buf));
                printf("%s\n", buf);
                close(sockfd);
                Exit = 1;
                break;

            case 100:
                getMenu();
                break;
        }
            
        if(Exit)
            break;
        
        //서버로 부터 구조체를 받아 옴
        recv(sockfd, &cData, sizeof(cData), MSG_WAITALL);
        printBook();
    }
}


void printBook() { //노트북 정보를 보여주기 위한 함수
    int i, fields, cnt;

    mysql_init(&mysql);
    mysql_set_character_set(&mysql, "utf8");
    
    if(!mysql_real_connect(&mysql, NULL, "hyerin", "me4023hy", "library", 3306, (char *)NULL, 0))
    {
        printf("%s \n", mysql_error(&mysql));
        exit(1);
    }
    if(mysql_query(&mysql, "SELECT * FROM book"))
    {
        printf("%s \n", mysql_error(&mysql));
        exit(1);
    }

    res = mysql_store_result(&mysql);
    fields = mysql_num_fields(res);

    puts("\n------Book------");
    
    while((row=mysql_fetch_row(res)))
    {
        for(cnt=0; cnt<fields; cnt++)
            printf("%12s ", row[cnt]);

        printf("\n");
    }

        
    puts("-----------------");
}

void getMenu() { //클라이언트 노트의 메뉴
    puts("원하는 번호를 입력 하세요.");
    puts("1. 도서 추가");
    puts("2. 도서 수정");
    puts("3. 도서 삭제");
    puts("98. 전체 내용 보기");
    puts("99. Exit");
}

int main(int argc, char *argv[]) {

    mysql_init(&mysql);
    mysql_set_character_set(&mysql, "utf8");
    
    if(!mysql_real_connect(&mysql, NULL, "hyerin", "me4023hy", "library", 3306, (char *)NULL, 0))
    {
        printf("%s \n", mysql_error(&mysql));
        exit(1);
    }

    noteClient(argv);
    return 0;
} 
