#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <mysql.h>
#include <sys/uio.h>

typedef struct //사용자 정보를 저장하는 구조체
{ 
    int userID;
    char ID[12];
    int createTime;
    int updateTime;
}userinfo;


typedef struct
{
    char name[30];
    int userID;
        char company[30];
    char writer[30];
        int book_num;
    int stat; // 도서 상태 식별. 0-> 도서관 내 소장 1-> 대여중 2-> 이 번호는 책이 없음.
}bookinfo;

typedef struct { //사용자, 도서를 한번에 관리하기 위한 구조체
    userinfo userInfo[30];
    bookinfo book[2000];
}serverData;



typedef struct { //클라이언트에 전송하기 위한 구조체
    bookinfo book[2048];
}clientData;

//클라이언트와 통신에 사용하기 위한 구조체
//Data Edit, Submit
typedef struct {
    int dataInputA;
    char name1[50];
    char name2[50];
    char date1[32];
    char company1[40];
    char writer1[40];
    int sub_num;
}dataSubmit;

//사용자 세션을 저장하기 위한 구조체
typedef struct {
    int session;
    int userID;
    int type;
}userSession;

#define MAXLINE 2048 //buf size
#define LISTENQ 100 //ListenQ
#define THREADNUM 30 //Client connect Num

//cleint 요청 값
#define CLIID 0 //ID
#define CLIWRITE 1 //입력
#define CLIEDIT 2 //수정
#define CLIDELETE 3 //삭제
#define CLIEXIT 99
#define NOTEDATA 100
#define DB_USER "hyerin"
#define DB_PASS "me4023hy"
#define DB_NAME "library"

serverData sData;

userSession session[30];
MYSQL mysql;
MYSQL *connection = NULL, conn;
MYSQL_RES *res;
MYSQL_ROW row; 

struct iovec iov[3];
struct iovec iov1[3];
ssize_t nr;
    

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
int sessionCnt = 0; //session count
void bookServer(char *argv[]); //서버 시작
void *thrfunc(void *arg); //Thread
void createStruct();


//Session Check
void initSession(); //세션 초기화
userSession userCreateAndCheck(char *str); //사용자 체크
int userSessionCheck(userSession *s); //사용자 Session Check

//노트 관련 함수
void bookCtrl(dataSubmit *d, userSession *s, int type, MYSQL*); //도서 Ctrl
void createBook(dataSubmit *d, userSession *s, MYSQL*); //새로운 도서 추가
void updateBook(dataSubmit *d, userSession *s, MYSQL*); //도서 내용 수정
void deleteBook(dataSubmit *d, userSession *s, int type, MYSQL*); //도서 삭제

//Client Data
void submitClient(userSession *s); //동일한 세션이 있을 경우 데이터 전송
clientData getClientData(userSession *s); //Client Data 복사
void setFile(void *arg, size_t size); //파일을 저장하는 부분
void *getFile(void *arg, size_t size); //파일을 불러 오는 부분


void *getFile(void *arg, size_t size) {
    FILE *f;

    if((f = fopen("user.dat", "r")) == NULL) {
        printf("connot open file for reding\n");
        exit(1);
    }
    fread(arg, size, 1, f);
    fclose(f);
    return arg;
}

void setFile(void *arg, size_t size) {
    FILE *f;
    
    if((f = fopen("user.dat", "w")) == NULL) {
        printf("connot open ffile for writting.\n");
        exit(1);
    }
    fwrite(arg, size, 1, f);
    fclose(f);
}


void bookServer(char *argv[]) { //서버를 생성하는 부분
    struct sockaddr_in servaddr, cliaddr;
    int listen_sock, accp_sock, status;
    socklen_t addrlen = sizeof(servaddr);
    pthread_t tid;

    //session 초기화
    initSession();

    if((listen_sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket Fail");
        exit(0);
    }
    
    memset(&servaddr, 0, sizeof(servaddr)); //0으로 초기화
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(atoi(argv[1]));

    //bind 호출
    if(bind(listen_sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind Fail");
        exit(0);
    }

    while(1) {
        listen(listen_sock, LISTENQ);

        accp_sock = accept(listen_sock, (struct sockaddr *)&cliaddr, &addrlen);
        if(accp_sock < 0) {
            perror("accept fail");
            exit(0);
        }

                /*클라이언트 마다 새로운 쓰레드를 생성한다.
                    해당 부분에서는 새로운 쓰레드를 생성하는 코드만 작성하고,  종료 코드를 적용하지 않는다.
                    만약 기존 예제 코드에 있던 쓰레드 종료 대기코드를 작성하게 되면
                    1명의 사용자 쓰레드가 종료되기 전까지는 새로운 사용자가 쓰레드로 접근하여 
                    작업을 진행 할 수 없다. 그렇기에 self 쓰레드 종료를 사용 함 */
        if((status = pthread_create(&tid, NULL, &thrfunc, (void *)&accp_sock)) != 0) {
            printf("thread create error: %s\n", strerror(status));
            exit(0);
        }
    }
}

void *thrfunc(void *arg) { //클라이언트마다 각각의 쓰레드 처리
    int accp_sock = (int) *((int*)arg);
    char buf[2048];
    int nbyte, i;
    userSession uSession; //세션을 보내고 받기해 생성
    clientData cData; //Client와 데이터를 주고 받기 위한 구조체
    MYSQL *con;
    int temp, temp1;

       //쓰레드를 스스로 종료하기 위해서 아래 코드를 작성
    pthread_detach(pthread_self());

    //사용자 계정 입력 대기
    puts("ID 입력 대기");
    if((nbyte = read(accp_sock, &buf, sizeof(buf))) < 0) {
        perror("read fail");
        exit(0);
    }
    buf[nbyte] = 0;
    printf("Client connect id : %s\n", buf);
    uSession = userCreateAndCheck(buf);

    write(accp_sock, &uSession, sizeof(userSession));
    printf("session:%d, userID:%d\n", uSession.session, uSession.userID);
    read(accp_sock, &buf, sizeof(buf));

    cData = getClientData(&uSession);
    send(accp_sock, &cData, sizeof(clientData), 0);

    //명령 실행
    while(1) 
    {
        bzero(&cData, sizeof(clientData));

        buf[0] = '\0';
        puts("Client Command Wait...");
        read(accp_sock, &uSession, sizeof(uSession));
        printf("%d User Select Type : %d\n", uSession.userID, uSession.type);

        int t = userSessionCheck(&uSession);
        write(accp_sock, &t, sizeof(int));
    
        if(t == -1) {
            puts("Client Session Error.....");
            close(accp_sock);
            break;
        }
    
        dataSubmit dSub;
    
        switch(uSession.type) {
            case CLIWRITE:
                printf("%d User Book Input\n", uSession.userID);

                iov[0].iov_base = dSub.name1;
                read(accp_sock, &temp, sizeof(temp));
                write(accp_sock, NULL, 0);    
                iov[0].iov_len = temp;

                iov[1].iov_base = dSub.writer1;
                read(accp_sock, &temp, sizeof(temp));
                write(accp_sock, NULL, 0);
                iov[1].iov_len = temp;
    
                iov[2].iov_base = dSub.company1;
                read(accp_sock, &temp, sizeof(temp));
                write(accp_sock, NULL, 0);
                iov[2].iov_len = temp;        
                        

                nr = readv(accp_sock, iov, 3);
                if(nr == -1)
                    printf("readv error");

                // read(accp_sock, &dSub, sizeof(dSub));
                printf("Book Num %d \n", dSub.sub_num);
                bookCtrl(&dSub, &uSession, 0, con);
                break;

            case CLIEDIT:
                printf("%d User Book Edit\n", uSession.userID);
                read(accp_sock, &dSub, sizeof(dSub));
                printf("Update Book Name : %s \n", dSub.name1);
                
                iov1[0].iov_base = dSub.name2;
                read(accp_sock, &temp1, sizeof(temp1));
                write(accp_sock, NULL, 0);    
                iov1[0].iov_len = temp1;

                iov1[1].iov_base = dSub.writer1;
                read(accp_sock, &temp1, sizeof(temp1));
                write(accp_sock, NULL, 0);
                iov1[1].iov_len = temp1;
    
                iov1[2].iov_base = dSub.company1;
                read(accp_sock, &temp1, sizeof(temp1));
                write(accp_sock, NULL, 0);
                iov1[2].iov_len = temp1;

                nr = readv(accp_sock, iov1, 3);
                if(nr == -1)
                    printf("readv error");
                bookCtrl(&dSub, &uSession, 1, con);
                break;

            case CLIDELETE:
                printf("%d User Book Delete\n", uSession.userID);
                read(accp_sock, &dSub, sizeof(dSub));
                printf("Delete Book Name : %s \n", dSub.name1);
                bookCtrl(&dSub, &uSession, 2, con);
                break;
                
                    
            case CLIEXIT:
                puts("Client exit");
                strcpy(buf, "exit");
                write(accp_sock, &buf, sizeof(buf));
                close(accp_sock); //클라이언트가 닫혔기 때문에 종료한다
                pthread_exit((void*)NULL); //쓰레드를 종료한다.
                break;
            }
    
        cData = getClientData(&uSession);
        send(accp_sock, &cData, sizeof(clientData), 0);
        bzero(&cData, sizeof(cData));
    }
    
    return ((void*)NULL);
}



//세션을 초기화 하기 위한 코드
void initSession() {
    int i;
    for(i=0;i<30;i++) {
        session[i].session = -1;
        session[i].userID = -1;
        session[i].type = -1;
    }
}

//새로운 사용자 인지 기존 사용자인지 세션을 체크
userSession userCreateAndCheck(char *str) {
    userSession tmpSession;
    int i;
    time_t timer;
    MYSQL *con;
    
    for(i=0; i < 30; i++) {
        if((strcmp(sData.userInfo[i].ID, str) == 0) && strcmp(sData.userInfo[i].ID, "0") != 0) {
            puts("new Session Create");
            pthread_mutex_lock(&lock);
            session[sessionCnt].session = sessionCnt;
            session[sessionCnt].userID = sData.userInfo[i].userID;
            session[sessionCnt].type = 0;
            tmpSession = session[sessionCnt];
            sessionCnt++;
            pthread_mutex_unlock(&lock);
            break;

        } else if(strcmp(sData.userInfo[i].ID, "0") == 0) {
            puts("new User & new Session Create");
            pthread_mutex_lock(&lock);
            timer = time(NULL);
            sData.userInfo[i].userID = ((i+1) * 10);
            strcpy(sData.userInfo[i].ID, str);
            sData.userInfo[i].createTime = timer;
            sData.userInfo[i].updateTime = timer;
            session[sessionCnt].session = sessionCnt;
            session[sessionCnt].userID = sData.userInfo[i].userID;
            session[sessionCnt].type = 0;


            tmpSession = session[sessionCnt];
            sessionCnt++;
            setFile((void *)&sData, sizeof(sData)); //Data Save
            pthread_mutex_unlock(&lock);
            break;
        }
    }
    return tmpSession;
}

//세션이 정상인지 체크
int userSessionCheck(userSession *s) {
    int i;
    for(i=0;i<30;i++) {
        if(session[i].session == s->session && session[i].userID == s->userID && session[i].session != -1) {
            return i;
        } else if(session[i].session == -1 && session[i].userID == -1)
            return -1;
    }
    return 0;
}


//도서 삭제, 추가, 수정을 위한 Ctrl
void bookCtrl(dataSubmit *d, userSession *s, int type, MYSQL *connection) {
    time_t timer = time(NULL);
    int i;
    
    pthread_mutex_lock(&lock);
    sData.userInfo[s->userID].updateTime = timer;

    mysql_init(&conn);
    mysql_set_character_set(&conn, "utf8");
    
    connection = mysql_real_connect(&conn, NULL, DB_USER, DB_PASS, DB_NAME, 3306, (char *)NULL, 0);
        if(connection == NULL){
                printf("connect error!\n");
                exit(1);
        }

    switch(type) {
        case 0: //insert
            createBook(d, s, connection);
            break;
        case 1: //update
            printf("도서 수정 함수 \n");
            updateBook(d, s, connection);
            break;
        case 2: //delete
            printf("도서 삭제 함수 \n");
            deleteBook(d, s, 0, connection);
            break;
    }
    setFile((void*)&sData, sizeof(sData)); //Data 저장
    pthread_mutex_unlock(&lock);
}

//새로운 도서 추가
void createBook(dataSubmit *d, userSession *s, MYSQL *connection) {
    int i;
    char buff[255];
    
    for(i=0; i<100;i++) {
        if(sData.book[i].book_num ==  -1) {
            puts("new book Create");
            sData.book[i].userID = s->userID;
            sData.book[i].book_num = d->dataInputA;
            strcpy(sData.book[i].name, (char*)iov[0].iov_base);
            strcpy(sData.book[i].writer, (char*)iov[1].iov_base);
            strcpy(sData.book[i].company, (char*)iov[2].iov_base);

            sprintf(buff, "insert into book(book_name, book_date, book_writer, book_company, book_num) values('%s', now(), '%s', '%s', '%d')",sData.book[i].name, (char*)iov[1].iov_base, (char*)iov[2].iov_base, sData.book[i].book_num);
            printf("%s : %p: %d\n",buff, connection,mysql_query(connection,buff));
            break;
        }
    }
}

//도서 수정
void updateBook(dataSubmit *d, userSession *s, MYSQL *con) {
    int i;
    char buff[255];
            
        
    for(i=0;i<100;i++) {
        if(strcmp(sData.book[i].name, d->name1)==0  && sData.book[i].userID == s->userID) {
            puts("Edit Book");
            strcpy(sData.book[i].name, (char *)iov1[0].iov_base);
            strcpy(sData.book[i].writer, (char *)iov1[1].iov_base);
            strcpy(sData.book[i].company, (char *)iov1[2].iov_base);
            
            sprintf(buff, "update book set book_name='%s', book_writer='%s', book_company='%s' where book_name='%s'",sData.book[i].name, sData.book[i].writer, sData.book[i].company, d->name1);
        
            printf("%s : %p: %d\n",buff, con,mysql_query(con,buff));
            break;
        }
    }
}

//도서 삭제 
void deleteBook(dataSubmit *d, userSession *s, int type, MYSQL *con) {
    char buff[255];
    int i;

    for(i=0; i<100; i++)
    {
        if(strcmp(sData.book[i].name, d->name1)==0 && sData.book[i].userID == s->userID) 
        {
            puts("Delete Book");
            sData.book[i].userID = -1;
            sData.book[i].book_num = -1;
            strcpy(sData.book[i].name, "0");
            strcpy(sData.book[i].writer, "0");
            strcpy(sData.book[i].company, "0");

            sprintf(buff, "delete from book where book_name='%s'", d->name1);
                    printf("%s : %p: %d\n",buff, con,mysql_query(con,buff));        
        }
    }
}



//Client에서 요청을 할 경우 해당 클라이언트의 데이터를 복사
clientData getClientData(userSession *s) {
    clientData cData;
    int i, k;
    for(i=0;i<100;i++) {
        cData.book[i].userID = -1;
        cData.book[i].book_num = -1;
        strcpy(cData.book[i].name, "0");
        strcpy(cData.book[i].writer, "0");
        strcpy(cData.book[i].company, "0");
    }
    
    k = 0;
    for(i=0;i<100;i++) {
        if(s->userID == sData.book[i].userID && sData.book[i].userID != -1) {
            cData.book[k].book_num = sData.book[i].book_num;
            cData.book[k].userID = sData.book[i].userID;
            strcpy(cData.book[k].name, sData.book[i].name);
            strcpy(cData.book[k].writer, sData.book[i].writer);
            strcpy(cData.book[k].company, sData.book[i].company);
            printf("%d, %s %s %s \n", cData.book[k].book_num, cData.book[k].name, cData.book[k].writer, cData.book[k].company);
            k++;
        }
    }
    return cData;
}


int main(int argc, char *argv[]) {

    if((fopen("user.dat", "r")) == NULL) {
               //저장한 파일이 없을 경우 처리
        createStruct(); //struct 초기화
        setFile((void *)&sData, sizeof(sData));
    }
    mysql_init(&mysql);
    mysql_set_character_set(&mysql, "utf8");

    connection = mysql_real_connect(&mysql, NULL, DB_USER, DB_PASS, DB_NAME, 3306, (char *)NULL, 0);
        if(connection == NULL){
                printf("connect error!\n");
                exit(1);
        }

     //저장된 데이터를 불러온다.
    sData = (serverData) *((serverData*)getFile((void *)&sData, sizeof(sData)));
    bookServer(argv);

    return 0;
}

void createStruct() {
    int i;

    //user info
    for(i = 0; i<30;i++) {
        sData.userInfo[i].userID = -1;
        strcpy(sData.userInfo[i].ID, "0");
        sData.userInfo[i].createTime = -1;
        sData.userInfo[i].updateTime = -1;
    }
    
    //나머지 부분 초기화
    for(i = 0; i<100;i++) {
        sData.book[i].book_num = -1;
        sData.book[i].userID = -1;
        strcpy(sData.book[i].name, "0");
        strcpy(sData.book[i].writer, "0");
        strcpy(sData.book[i].company, "0");
    }
} 