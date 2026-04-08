#include "executor.h" // 나의 뼈대가 된 실행기 종결자 헤더를 가져옵니다.

#include "storage.h" // 실행한다는것은 실제로 하드 디스크 파일을 조작해야 함을 의미하기 때문에 스토리지 모듈 헤더를 당연히 부릅니다.
#include "util.h" // 자잘한 배열 메모리와 문자열 할당들을 손쉽고 깨끗하게 다루기 위해 가져옵니다.

#include <stdio.h> // 화면에 에러를 기록하거나(snprintf) 결과값을 유저 화면에 예쁘게 표출, 출력(printf)하기 위한 헤더입니다.
#include <stdlib.h> // realloc 으로 가변 배열 동적 메모리를 확장증축하며 다루기 위해 쓰입니다.
#include <string.h> // 배열 메모리 등을 일괄 초기화 등에 쓰입니다.

// 스토리지 하드 공간에 무턱대고 모든 데이터 줄들을 다 퍼오게 시키지 않기 위해, SELECT 나 옵션WHERE 에서 유저가 '진짜 요구했던' 컬럼들 이름만 담아 로더에 보내기 위한 1차원 리스트 구조체
typedef struct {
    char **items; // 필요한 요구 기둥(컬럼) 이름 텍스트 배열들의 집합소
    size_t count; // 그 아이템 개수
} RequiredColumnList;

// 위 요구명단 배열과 내용을 쓰고 난 후 할당 메모리 자재를 싹 비워주는 보조 함수
static void free_required_columns(RequiredColumnList *columns) {
    if (columns == NULL) { return; } // 비었으면 공치지 않고 즉각 패스
    sql_free_string_array(columns->items, columns->count); // 안에 든 문자 배열들을 하나하나 다 무자비하게 지워줌
    columns->items = NULL; // 껍데기도 깨끗
    columns->count = 0;
}

// 요구명단 배열에 뽑아야 될 필요한 컬럼 이름들을 차근차근 하나씩 위로 쌓아연장(Append) 올려주는 함수
static int append_required_column(RequiredColumnList *columns, const char *column_name, char *error_buf, size_t error_buf_size) {
    char **new_items;
    char *copy;

    // 만일 이 이름("id", "name" 등)이 이미 추가 명단에 유저나 시스템에 의해 똑같이 요구되어 중복 등록 들어가 있다면
    if (sql_string_array_contains(columns->items, columns->count, column_name)) {
        return 1; // 또 중복으로 달라고 적을 필요가 없으니 불필요 메모리 낭비 방지를 위해 바로 스킵하며 성공처리(1)!
    }

    // 명단에 안 적혀있었으면 비좁으므로 새 공간 포인터를 요구 길이+1 로 강제 늘리고
    new_items = (char **)realloc(columns->items, (columns->count + 1) * sizeof(char *));
    if (new_items == NULL) {
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        return 0; // 메모리 증설 부족 실패
    }
    columns->items = new_items; // 새 안전 주소 갱신 할당

    copy = sql_strdup(column_name); // 요구 이름 문자열 메모리 깊은 복사
    if (copy == NULL) {
        snprintf(error_buf, error_buf_size, "메모리 할당에 실패했습니다.");
        return 0; // 에러
    }

    columns->items[columns->count] = copy; // 증설된 맨 끝방 인덱스 자리에 명단 이름 당당히 할당 입주!
    columns->count++; // 사람(단어) 하나 늘었음!
    return 1; // 무사 추가 1 보고
}

// 파싱이 끝난 SELECT 명령 구조체(select_stmt)를 속속들이 뒤져서 "그래서 결론, 우리 지금 어떤 기둥기둥들이 필요한거야?" 하고 요구 리스트(required_columns)를 한 덩어리로 빌드 조립해 주는 추출 분석 함수
static int build_required_columns(const SelectStatement *select_stmt, RequiredColumnList *required_columns, char *error_buf, size_t error_buf_size) {
    size_t index;

    memset(required_columns, 0, sizeof(*required_columns)); // 제출할 결과 서류인 명단 메모리를 백지화 깨끗한 세탁

    if (select_stmt->columns.is_star) { // 만일 트리 구조체가 "전부 다 줘(*)" 와일드카드 모드라면 
        return 1; // 편식할 거 없이 풀매수 스루 모드기때문에 요구 명단을 아예 텅비운 채 이지 통과시킵니다. (나중에 storage 모듈이 명단이 비어있는걸 보면 알아서 전체를 퍼옵니다)
    }

    // 만약 (*) 가 아니고 특정 제한이라면 셀렉트에 적힌 편식 컬럼 개수만큼 빙글빙글 루프를 돌면서
    for (index = 0; index < select_stmt->columns.count; ++index) {
        // 요구 명단 구조체에 그 해당 편식 이름들을 차곡차곡 기입 팍팍 추가해 줍니다!
        if (!append_required_column(required_columns, select_stmt->columns.items[index], error_buf, error_buf_size)) {
            free_required_columns(required_columns); // 메모리 오링 실패 시 적던 스케치북 파쇄 터뜨림
            return 0;
        }
    }

    // 단, 오잉? 조회를 분명히 원하는 컬럼 말고도, 아래쪽을 보니 값을 필터링해야 할 조건절(WHERE) 옵션이 은밀히 켜져있었다면?
    if (select_stmt->has_where) {
        // 옵션 WHERE 절 에서 비교 기준으로 쓰일 기둥(예: id) 도 결국 값을 하드에서 가져와 알아야 비교 판가름을 할 수 할 수 있기 때문에, 이놈도 필수 요구 명단에 몰래 슬쩍 적어서 등록해줍니다!
        if (!append_required_column(required_columns, select_stmt->where.column_name, error_buf, error_buf_size)) {
            free_required_columns(required_columns); // 몰래 적다 실패 시 튕김
            return 0;
        }
    }

    return 1; // 무사히 모든 필수 퍼와야할 요구사항 기둥 컬럼명 작성 완료!
}

// 스토리지 모듈에서 꿀렁 꿀렁 메모리로 올라온 거대한 2차원 테이블 데이터 중 특정 한 줄(row_index)이, 사용자가 건 제약조건(WHERE)에 일치하는 합격 녀석인지 아닌지 진실 판가름하는 판사 필터함수
static int row_matches_filter(const TableData *table, size_t row_index, const SelectStatement *select_stmt) {
    int column_index;

    // 애초에 구문에 제약을 거는 조건(has_where) 플래그가 안 달려 있었다면
    if (!select_stmt->has_where) {
        return 1; // 필터링 할 게 1도 없으니 무조건 다통과(참, 프리패스 1 반환)
    }

    // 조건절에 기준이 될 컬럼(예: id) 이라는게 과연 실제 테이블 내부의 도대체 몇 번째 인덱스 숫자 방 위치냐? 를 빠른 배열 참조 탐색기 통해 찾습니다.
    column_index = storage_find_column_index(table, select_stmt->where.column_name);
    if (column_index < 0) { // 만약 애초에 그 테이블에 그딴 비교할 컬럼이 없다면
        return 0; // 넌 비교 할것도 없이 당첨 탈락! 거짓 반환.
    }

    // 드디어 찾아낸 해당 가로줄 데이터의 필터 대상 기둥 위치 값과, 구문에서 요구했던 조건 비교상수(예: 1) 철자가 동일한지(strcmp) 검사합니다.
    return strcmp(table->rows[row_index][column_index], select_stmt->where.value.text) == 0; // 같다면 1(참), 다르면 0(거짓) 으로 매 행마다 평가 통과 탈락을 결정합니다
}

// 메모리에 추출된 최종 출력용 값들의 배열이 넘어오면, 각 글자 단어들 사이에 콤마(,)를 예쁘게 끼워넣고 화면 표준 출력기(C_Tester 터미널)에 시원하게 쏘아 프린트해주는 전시용 함수
static void print_csv_line(char *const *values, size_t count) {
    size_t index;

    for (index = 0; index < count; ++index) {
        if (index > 0) { 
            printf(","); // 맨 처음타를 뺀 단어 사이사이에 이음새 콤마를 화면 한켠 터미널에 살짝 출력
        }
        printf("%s", values[index]); // 진품 알맹이 글자들을 차례차례 화면에 시각적으로 출력 전시
    }
    printf("\n"); // 아름답게 1줄을 다 이어 뿜었으면 이쁘게 다이얼로그 개행 엔터 바꿈!
}

// 명령어가 SELECT 로 최종 밝혀졌을 때 구동 지시되는 조회 명령 전용 심장 실행기
static int execute_select(const SelectStatement *select_stmt, const char *data_dir, char *error_buf, size_t error_buf_size) {
    TableData table; // 퍼올 테이블 결과물을 메모리에 크게 담을 거대한 항아리 그릇
    RequiredColumnList required_columns; // 앞서 만든 편식 요구사항 리스트 객체
    size_t row_index; 
    size_t output_count;
    size_t column_index;
    char **output_columns;

    memset(&table, 0, sizeof(table)); // 쓰레기 제거 테이블 항아리 물청소
    memset(&required_columns, 0, sizeof(required_columns)); // 리스트 초기 청소

    // SELECT 구문 내부 트리를 후벼파 읽고 "나 스토리지 너한테 이러이러한 기둥 좀 퍼다달라고 할게" 명단을 작성시킵니다.
    if (!build_required_columns(select_stmt, &required_columns, error_buf, error_buf_size)) {
        return 0;
    }

    // 조립 작성된 명단(required_columns.items)을 스토리지 모듈 메인 로더로 넘겨 강려크하게 일시킵니다. 디스크에서 메모리로 테이블(table) 그릇에 데이터가 콸콸콸!
    if (!storage_load_table(data_dir, select_stmt->table_name, required_columns.items, required_columns.count, &table, error_buf, error_buf_size)) {
        free_required_columns(&required_columns); // 메모리 터지고 장사 실패시 주변기기 정리 철수
        return 0;
    }

    free_required_columns(&required_columns); // 무사 완료되었으면, 이쪽 요구 리스트 종이는 제 몫을 다 했으니 여기서 퇴역폐쇄 시킵니다.

    if (select_stmt->columns.is_star) { // 만일 (*) 즉 걍 있는건 필터 없이 있는대로 다 내놔 조회 모드 였다면
        print_csv_line(table.columns, table.column_count); // 헤더 기둥 명칭들부터 터미널 화면에 일단 쫙 다 뿜어 헤더장식을 그립니다
        for (row_index = 0; row_index < table.row_count; ++row_index) { // 하이라이트. 긁어온 알맹이 줄들을 첫줄부터 바닥까지 샅샅이 뒤져가며 
            if (row_matches_filter(&table, row_index, select_stmt)) { // 옵션인 WHERE 구문 필터에 부합하는 조건 합격 줄인지 판사를 대동해 검사해서
                print_csv_line(table.rows[row_index], table.column_count); // 당첨 판정이 난 줄만 터미널 화면에 쫙쫙 출력시킵니다!
            }
        }
        storage_free_table(&table); // 일 다했으니 뱃속 무지막지한 2차원 메모리 테이블 폐기 삭제
        return 1; // 대 성 공
    }

    // 만약 (*) 가 아니라 요리조리 편식 컬럼 조회였다면, 쏴줄 특정 기둥 이름과 갯수를 다시 재참조해 받아놓습니다.
    output_columns = select_stmt->columns.items;
    output_count = select_stmt->columns.count;
    print_csv_line(output_columns, output_count); // 이것도 일단 뽑을 필터 헤더 타이틀들의 이름들만 터미널 상단에 뽝 출력시켜 줍니다.

    // 긁어온 엄청난 양의 행(row)을 또 위에서부터 쭉 돌리며 출력을 대비하는데
    for (row_index = 0; row_index < table.row_count; ++row_index) {
        if (!row_matches_filter(&table, row_index, select_stmt)) { // 이번 행이 WHERE 조건 필터에 못미치는 불합격 행이라면
            continue; // 가차없이 아래를 스킵 버리고 출력을 스킵하여 패스합니다!
        }

        // 오직 판사 필터에 합격한 진짜배기 줄이라면, 이제 이 줄 내부 안에서도 "내가 애초에 원했던 편식 기둥들 값"만 쪽쪽 빨아서 밖으로 출력할 시간입니다.
        for (column_index = 0; column_index < output_count; ++column_index) {
            // 내가 표출 원하는 편식 지정 컬럼의 이름이, "현재 스토리지에서 뽑아온 테이블 메모리 구조"에서는 몇 번재 인덱스 방인지 그 방 번호를 알아냅니다.
            int selected_index = storage_find_column_index(&table, output_columns[column_index]);
            if (selected_index < 0) { // 오잉, 퍼오라고 시켰는데 안 퍼와졌다면
                storage_free_table(&table); // 야마돌면서 밥상 다 엎고 메모리 회수 처리
                snprintf(error_buf, error_buf_size, "실행 중 존재하지 않는 조작 컬럼을 찾았습니다: %s", output_columns[column_index]);
                return 0; // 심각한 오류 리턴
            }

            if (column_index > 0) {
                printf(","); // 첫방 출력이 아니면 콤마를 사이사이에 예쁘게 찍어주고
            }
            // 매칭된 진짜 방번호(selected_index) 를 활용해 내가 원하는 방 인덱스의 데이터 값만 꺼내서 터미널 화면에 속속 프린팅 이식시킵니다!
            printf("%s", table.rows[row_index][selected_index]);
        }
        printf("\n"); // 힘들게 필터된 한 줄의 여러 속성이 터미널에 무사히 찍힐 때마다, 다음 녀석을 위해 줄바꿈 개행!
    }

    storage_free_table(&table); // 모든 행, 열의 터미널 출력이 끝나면 거대 테이블 메모리를 모조리 삭제 파쇄합니다.
    return 1; // 조회 및 터미널 출력 실행 완전 대성공! (1 반환)
}

// 명령어가 INSERT 로 밝혀졌을 때 곧바로 가동 스위치가 눌리는 삽입 데이터 추가 명령 전용 심장 실행기
static int execute_insert(const InsertStatement *insert_stmt, const char *data_dir, char *error_buf, size_t error_buf_size) {
    // 거창한 거 없이 로직이 간단합니다, 파서가 잘 넘겨준 삽입값(values) 1차원 데이터 베열 그 자체를 스토리지 모듈 하드 파일 맨 끝단 밑바닥 쑤셔넣기 추가(Append) 함수에 외주 하청시켜 턱 던집니다.
    if (!storage_append_row(data_dir,
                            insert_stmt->table_name,
                            insert_stmt->columns.items,
                            insert_stmt->columns.count,
                            insert_stmt->values,
                            insert_stmt->value_count,
                            error_buf,
                            error_buf_size)) {
        return 0; // 공장 돌리며 던졌다 터졌으면 실패 0 반환
    }

    // 무사히 스토리지의 하드 디스크 파일 내부에 물리적 데이터 박음질 저장이 끝나고 나면, 유저가 기분좋게 터미널에서 알수있도록 성공 안내 멘트를 남겨줍니다.
    printf("Inserted 1 row into %s\n", insert_stmt->table_name);
    return 1; // 무사 정상 임무 종료 (1 반환)
}

// **종결자 통수권자 대장 본체**: 모든 파싱과 최적화가 끝나고 넘겨받은 반짝이는 명령 트리(Statement 래퍼) 구조체를 가지고, 안에 있는 부대마크가 INSERT 냐 SELECT 냐에 따라 세부 실행 부대에게 지시를 위임해 실구동을 발발하는 최종 보스!
int execute_statement(const Statement *statement, const char *data_dir, char *error_buf, size_t error_buf_size) {
    if (statement == NULL) { // 여기까지 올라와 나에게 제출된 실행 결재 서류가 텅빈 백지라면
        snprintf(error_buf, error_buf_size, "실행할 문장이 없습니다.");
        return 0; // 거부 및 결재 서류 파기
    }

    // 넘겨받은 래퍼 명령 트리의 최종 목적(type 마크)이 데이터 '삽입 추가'라면
    if (statement->type == AST_INSERT_STATEMENT) {
        // 전담 삽입 부대(execute_insert)에게 공용체 영역과 부대조건들을 넘겨주어 곧바로 파일 수술에 투입시킵니다
        return execute_insert(&statement->as.insert_stmt, data_dir, error_buf, error_buf_size);
    }

    // 그 외(SELECT)라면 파일 내용들을 조회하여 읽고 솎아 올, 조회 스캐너 부대(execute_select)에게 조건들을 넘겨 투입 완료시킵니다.
    return execute_select(&statement->as.select_stmt, data_dir, error_buf, error_buf_size);
}
