#include "ast.h" // 위에서 주석화하여 정의한 구문 트리의 골격 포맷 헤더 포함

#include "util.h" // 여러 곳에 쓰이는 만능 메모리, 문자열 유틸리티 함수들을 끌어쓰기 위해 헤더 포함

#include <stdlib.h> // 메모리의 동적 해제 관련(free) 기본 내장 함수들을 위해 포함

// 저장해 두었던 단일 '값(Literal)' 데이터 찌꺼기 메모리를 시스템에 비워주는 함수 구현부
void free_literal(Literal *literal) {
    if (literal == NULL) { // 만일 애초에 구조체 포인터에 아무런 값도 안들어있다면 (비어있다면)
        return;            // 지울게 없으니 그냥 아무 행동도 안하고 바로 다음으로 넘어갑니다.
    }

    free(literal->text);   // 구조체 속에 값을 담기 위해 동적 할당했었던 실제 텍스트("1" 등) 공간을 해제시킵니다.
    literal->text = NULL;  // 혹시라도 이미 비운 메모리를 재참조하는 버그를 막기 위해 구조체의 text 포인터값을 강제로 NULL로 덮어씁니다.
}

// 기존 '값(Literal)' 구조체를 참조 없이 완벽히 똑같은 새 복사본 객체로 붕어빵 찍어내는 함수 구현부
Literal clone_literal(const Literal *literal) {
    Literal copy; // 새로운 값(만들어낼 복사본)을 쏙 담아둘 빈 구조체를 스택에 준비시킵니다.

    copy.type = literal->type; // 원본 구조체가 가리키던 타입 플래그(숫자인지 문자인지) 정보를 일단 복사체에 똑같이 심어줍니다.
    copy.text = sql_strdup(literal->text); // 가장 핵심! 우리가 util.c에서 만든 strdup 함수를 이용해 텍스트 데이터의 내용을 "완전 새로운 물리적 공간"에 그대로 똑같이 복사해넣습니다.
    return copy; // 주소가 겹치지 않는 완전한 깊은 복사가 무사히 다 끝난 완성 복사본 구조체를 반환합니다.
}

// 이 파일 내부에서만 조용히 꺼내 쓰는 전용 보조 함수(정적-static): 컬럼들의 리스트 배열을 모조리 싹 지워주는 함수 구현부
static void free_column_list(ColumnList *columns) {
    if (columns == NULL) { // 지우라고 요청받은 컬럼 리스트 구조체 자체가 비어(NULL)있다면
        return;            // 지울 행동 없이 함수 실행을 취소하고 그냥 복귀합니다.
    }

    sql_free_string_array(columns->items, columns->count); // 배열 내의 아이템들을 개수만큼 내부 아이템까지 통째로 날려주는 유틸 함수의 힘을 빌립니다.
    columns->items = NULL; // 내부 배열 메모리는 모두 날아갔으므로 구조체도 포인터를 상실하게 널로 초기화 해주고
    columns->count = 0;    // 아이템이 남아있지 않으므로 개수 마저 0으로 세탁, 리셋합니다.
}

// "명령 세트(Statement)" 실행이 모두 끝났을 때 그것이 거미줄처럼 잡아먹고있던 동적 자원을 훑으며 구조적으로 회수해 주는 함수 구현부
void free_statement(Statement *statement) {
    size_t index; // 리스트같이 줄줄이 엮인 항목들이 있을 시 루프를 돌리기 위한 인덱스 지정용 변수 생성

    if (statement == NULL) { // 회수 작업을 지시받은 큰 명령 구문 트리조차 메모리에 없다면
        return;              // 그대로 아무 행동 없이 반환합니다.
    }

    switch (statement->type) { // 공용체(union) 내부에 묻어놨던 type 표식을 통해 방금 어떤 명령 타입이었는지 스위치문으로 분기 진입합니다.
        case AST_SELECT_STATEMENT: // 만약 회수해야 할 대상이 "조회(SELECT) 명령" 유형이었다면, 
            free(statement->as.select_stmt.table_name); // SELECT 구조체 안에서 동적할당 받았던 '테이블 이름(table_name)' 할당을 일단 해제시킵니다.
            free_column_list(&statement->as.select_stmt.columns); // SELECT 구조체 안의 '조회 할지 말지 정했던 컬럼 목록들(columns)'의 메모리를 위에서 만든 함수에 떠밀어 해제시킵니다.
            if (statement->as.select_stmt.has_where) { // 만일 SELECT 문에 단순 조회가 아니라 심도있는 조건절(WHERE)도 붙어 구동됐었다면,
                free(statement->as.select_stmt.where.column_name); // 어떤 컬럼명으로 조건 조회했는지 할당되었던 메모리를 지워서 청소하고,
                free_literal(&statement->as.select_stmt.where.value); // 어떤 값(literal)을 찾아달라고 했었는지 값을 앞서 만든 함수를 통해 모두 비워냅니다.
            }
            break; // SELECT 메모리 해제 로직 블록에서 안전하게 탈출합니다.
        case AST_INSERT_STATEMENT: // 반면 해당 회수 명령 구조 대상이 메모리 추가(INSERT) 명령 쪽이었다면,
            free(statement->as.insert_stmt.table_name); // 어떤 테이블명 폴더에 넣을거였는지 이름(table_name) 자리 메모리를 반환하여 비웁니다.
            free_column_list(&statement->as.insert_stmt.columns); // INSERT 시 따로 적어두었던 입력 컬럼 순서 목록이 있다면 그것도 같이 회수합니다.
            for (index = 0; index < statement->as.insert_stmt.value_count; ++index) { // 해당 테이블에 삽입하려고 쌓아놨던 값(value)들의 배열을 갯수 카운터만큼 빙글빙글 돌면서 계속 반복합니다.
                free_literal(&statement->as.insert_stmt.values[index]); // 준비되어 배열 칸칸마다 꽂혀 있던 리터럴 구조체들의 값 메모리들을 하나씩 하나씩 꼼꼼하게 전부 해제해 줍니다.
            }
            free(statement->as.insert_stmt.values); // 안쪽의 각 값 구조체들이 다 무사히 산산조각나 빈집이 된 값(values)들의 배열 껍데기 자체 메모리를 운영체제로 다시 반환하여 마지막으로 통째로 쓰레기통에 날립니다.
            break; // INSERT 메모리 해제 로직 블록에서 탈출합니다.
    }
}
