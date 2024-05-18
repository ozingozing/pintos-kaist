Brand new pintos for Operating Systems and Lab (CS330), KAIST, by Youngjin Kwon.

The manual is available at https://casys-kaist.github.io/pintos-kaist/.

실행방법
추가로
cd ~ # home 디렉토리로 이동
code .bashrc 
하면 파일이 vscode에서 파일이 열릴거임
# 아래 내용 추가 
# source ~/pintos-kaist/activate
///////////////////////////////
pintos-kaist에 source ./activate
cd thread에서 make
cd builds에서 make check또는 test/thread/tests.c에서 원하는 테스트말고는 주석 떄리고 check 가능
아니면 pintos -v -k -T 480 -m 20   -- -q  -mlfqs run mlfqs-block이런 명령어도 가능