#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#endif

#define MAX_ROWS 2000
#define FEATURE_COUNT 13
#define SELECT_COUNT 4
#define LINE_BUF 1024

typedef struct {
    int index;
    double corr;
} CorrItem;

static double pearson_corr(const double *x, const double *y, int n) {
    double sum_x = 0.0, sum_y = 0.0, sum_x2 = 0.0, sum_y2 = 0.0, sum_xy = 0.0;
    for (int i = 0; i < n; ++i) {
        sum_x += x[i];
        sum_y += y[i];
        sum_x2 += x[i] * x[i];
        sum_y2 += y[i] * y[i];
        sum_xy += x[i] * y[i];
    }

    double numerator = n * sum_xy - sum_x * sum_y;
    double denominator_x = n * sum_x2 - sum_x * sum_x;
    double denominator_y = n * sum_y2 - sum_y * sum_y;

    if (denominator_x <= 0.0 || denominator_y <= 0.0) {
        return 0.0;
    }

    return numerator / sqrt(denominator_x * denominator_y);
}

static void swap_corr(CorrItem *a, CorrItem *b) {
    CorrItem temp = *a;
    *a = *b;
    *b = temp;
}

static void print_utf8f(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

#ifdef _WIN32
    int length = _vscprintf(fmt, args);
    if (length >= 0) {
        char *buffer = (char *)malloc((size_t)length + 1);
        if (buffer) {
            va_list args_copy;
            va_copy(args_copy, args);
            vsnprintf(buffer, (size_t)length + 1, fmt, args_copy);
            va_end(args_copy);

            HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
            DWORD mode = 0;
            if (handle != INVALID_HANDLE_VALUE && GetConsoleMode(handle, &mode)) {
                int wide_len = MultiByteToWideChar(CP_UTF8, 0, buffer, -1, NULL, 0);
                if (wide_len > 0) {
                    wchar_t *wide = (wchar_t *)malloc((size_t)wide_len * sizeof(wchar_t));
                    if (wide) {
                        MultiByteToWideChar(CP_UTF8, 0, buffer, -1, wide, wide_len);
                        DWORD written = 0;
                        WriteConsoleW(handle, wide, (DWORD)(wide_len - 1), &written, NULL);
                        free(wide);
                        free(buffer);
                        va_end(args);
                        return;
                    }
                }
            }

            fputs(buffer, stdout);
            free(buffer);
            va_end(args);
            return;
        }
    }
#endif

    vprintf(fmt, args);
    va_end(args);
}

static void sort_corr_desc(CorrItem items[], int n) {
    for (int i = 0; i < n - 1; ++i) {
        for (int j = 0; j < n - 1 - i; ++j) {
            if (fabs(items[j].corr) < fabs(items[j + 1].corr)) {
                swap_corr(&items[j], &items[j + 1]);
            }
        }
    }
}

static int invert_matrix(double *mat, double *inv, int n) {
    for (int i = 0; i < n * n; ++i) {
        inv[i] = 0.0;
    }
    for (int i = 0; i < n; ++i) {
        inv[i * n + i] = 1.0;
    }

    double *a = (double *)malloc((size_t)n * n * sizeof(double));
    if (!a) {
        return 0;
    }
    memcpy(a, mat, (size_t)n * n * sizeof(double));

    for (int i = 0; i < n; ++i) {
        int pivot = i;
        double max_val = fabs(a[i * n + i]);
        for (int r = i + 1; r < n; ++r) {
            double v = fabs(a[r * n + i]);
            if (v > max_val) {
                max_val = v;
                pivot = r;
            }
        }

        if (max_val < 1e-12) {
            free(a);
            return 0;
        }

        if (pivot != i) {
            for (int c = 0; c < n; ++c) {
                double t = a[i * n + c];
                a[i * n + c] = a[pivot * n + c];
                a[pivot * n + c] = t;

                t = inv[i * n + c];
                inv[i * n + c] = inv[pivot * n + c];
                inv[pivot * n + c] = t;
            }
        }

        double diag = a[i * n + i];
        for (int c = 0; c < n; ++c) {
            a[i * n + c] /= diag;
            inv[i * n + c] /= diag;
        }

        for (int r = 0; r < n; ++r) {
            if (r == i) continue;
            double factor = a[r * n + i];
            if (fabs(factor) < 1e-12) continue;
            for (int c = 0; c < n; ++c) {
                a[r * n + c] -= factor * a[i * n + c];
                inv[r * n + c] -= factor * inv[i * n + c];
            }
        }
    }

    free(a);
    return 1;
}

int main(int argc, char *argv[]) {
    setlocale(LC_ALL, "");
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    const char *filename = NULL;
    if (argc >= 2) {
        filename = argv[1];
    } else {
        filename = "housing-price.txt";
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        print_utf8f("无法打开文件: %s\n", filename);
        return 1;
    }

    double X[MAX_ROWS][FEATURE_COUNT];
    double y[MAX_ROWS];
    int n = 0;

    char line[LINE_BUF];
    while (fgets(line, sizeof(line), fp)) {
        if (strlen(line) < 2) continue;

        double values[FEATURE_COUNT + 1];
        int count = 0;
        char *token = strtok(line, " \t\r\n");
        while (token != NULL && count < FEATURE_COUNT + 1) {
            values[count++] = atof(token);
            token = strtok(NULL, " \t\r\n");
        }

        if (count != FEATURE_COUNT + 1) {
            continue;
        }

        if (n >= MAX_ROWS) {
            print_utf8f("数据行数超过上限 %d\n", MAX_ROWS);
            fclose(fp);
            return 1;
        }

        for (int j = 0; j < FEATURE_COUNT; ++j) {
            X[n][j] = values[j];
        }
        y[n] = values[FEATURE_COUNT];
        ++n;
    }

    fclose(fp);

    if (n < 5) {
        print_utf8f("有效数据太少，无法建模\n");
        return 1;
    }

    CorrItem corr_items[FEATURE_COUNT];
    for (int j = 0; j < FEATURE_COUNT; ++j) {
        double column[MAX_ROWS];
        for (int i = 0; i < n; ++i) {
            column[i] = X[i][j];
        }
        corr_items[j].index = j;
        corr_items[j].corr = pearson_corr(column, y, n);
    }

    sort_corr_desc(corr_items, FEATURE_COUNT);

    const char *feature_names[FEATURE_COUNT] = {
        "CRIM", "ZN", "INDUS", "CHAS", "NOX", "RM", "AGE",
        "DIS", "RAD", "TAX", "PTRATIO", "B", "LSTAT"
    };

    print_utf8f("13个特征与房价MEDV的相关系数:\n");
    for (int i = 0; i < FEATURE_COUNT; ++i) {
        int idx = corr_items[i].index;
        print_utf8f("%-8s  r = % .6f\n", feature_names[idx], corr_items[i].corr);
    }

    int selected[SELECT_COUNT];
    print_utf8f("\n相关性绝对值最高的4个特征:\n");
    for (int i = 0; i < SELECT_COUNT; ++i) {
        selected[i] = corr_items[i].index;
        print_utf8f("%d. %s  (r = % .6f)\n", i + 1, feature_names[selected[i]], corr_items[i].corr);
    }

    int p = SELECT_COUNT + 1;
    double *A = (double *)calloc((size_t)p * p, sizeof(double));
    double *b = (double *)calloc((size_t)p, sizeof(double));
    double *Ainv = (double *)calloc((size_t)p * p, sizeof(double));
    double *beta = (double *)calloc((size_t)p, sizeof(double));

    if (!A || !b || !Ainv || !beta) {
        print_utf8f("内存分配失败\n");
        free(A);
        free(b);
        free(Ainv);
        free(beta);
        return 1;
    }

    for (int i = 0; i < n; ++i) {
        double row[SELECT_COUNT + 1];
        row[0] = 1.0;
        for (int j = 0; j < SELECT_COUNT; ++j) {
            row[j + 1] = X[i][selected[j]];
        }

        for (int r = 0; r < p; ++r) {
            b[r] += row[r] * y[i];
            for (int c = 0; c < p; ++c) {
                A[r * p + c] += row[r] * row[c];
            }
        }
    }

    if (!invert_matrix(A, Ainv, p)) {
        print_utf8f("\n矩阵不可逆，无法求解回归系数。可能是特征共线性太强。\n");
        free(A);
        free(b);
        free(Ainv);
        free(beta);
        return 1;
    }

    for (int i = 0; i < p; ++i) {
        for (int j = 0; j < p; ++j) {
            beta[i] += Ainv[i * p + j] * b[j];
        }
    }

    double mse = 0.0;
    for (int i = 0; i < n; ++i) {
        double pred = beta[0];
        for (int j = 0; j < SELECT_COUNT; ++j) {
            pred += beta[j + 1] * X[i][selected[j]];
        }
        double err = pred - y[i];
        mse += err * err;
    }
    mse /= n;
    double rmse = sqrt(mse);

    print_utf8f("\n多元线性回归模型:\n");
    print_utf8f("y = %.6f", beta[0]);
    for (int j = 0; j < SELECT_COUNT; ++j) {
        print_utf8f(" + (%.6f) * %s", beta[j + 1], feature_names[selected[j]]);
    }
    print_utf8f("\n");

    print_utf8f("\nRMSE = %.6f\n", rmse);

    free(A);
    free(b);
    free(Ainv);
    free(beta);

    return 0;
}