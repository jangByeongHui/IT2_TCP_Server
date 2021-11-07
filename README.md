# IT2_TCP_Server
IT 집중교육2 2인 팀 프로젝트 Multi Thread TCP SERVER

#실행 파일 생성
make

#실행 파일 삭제

make clean

#model1 실행
./model1 &

#model2 실행
./model2 &

#client 연결
SERVERHOST=localhost ./echocli <Port NUM>
