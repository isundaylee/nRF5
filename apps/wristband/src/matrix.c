static bool       initilization          = true;

// Function to get cofactor of A[p][q] in temp[][]. n is current
// dimension of A[][]
void getCofactor(int A[][], int *temp[][], int p, int q, int n)
{
    int i = 0, j = 0;
 
    // Looping for each element of the matrix
    for (int row = 0; row < n; row++)
    {
        for (int col = 0; col < n; col++)
        {
            //  Copying into temporary matrix only those element
            //  which are not in given row and column
            if (row != p && col != q)
            {
                temp[i][j++] = A[row][col];
 
                // Row is filled, so increase row index and
                // reset col index
                if (j == n - 1)
                {
                    j = 0;
                    i++;
                }
            }
        }
    }
}
 
/* Recursive function for finding determinant of matrix.
   n is current dimension of A[][]. */
int determinant(int A[][], int n)
{
    int D = 0; // Initialize result
 
    //  Base case : if matrix contains single element
    if (n == 1)
        return A[0][0];
 
    int temp[n][n]; // To store cofactors
 
    int sign = 1;  // To store sign multiplier
 
     // Iterate for each element of first row
    for (int f = 0; f < n; f++)
    {
        // Getting Cofactor of A[0][f]
        getCofactor(A, temp, 0, f, n, N);
        D += sign * A[0][f] * determinant(temp, n - 1);
 
        // terms are to be added with alternate sign
        sign = -sign;
    }
 
    return D;
}
 
// Function to get adjoint of A[N][N] in adj[N][N].
void adjoint(int A[][],int *adj[][], int N)
{
    if (N == 1)
    {
        adj[0][0] = 1;
        return;
    }
 
    // temp is used to store cofactors of A[][]
    int sign = 1, temp[N][N];
 
    for (int i=0; i < N; i++)
    {
        for (int j=0; j < N; j++)
        {
            // Get cofactor of A[i][j]
            getCofactor(A, temp, i, j, N);
 
            // sign of adj[j][i] positive if sum of row
            // and column indexes is even.
            sign = ((i+j)%2==0)? 1: -1;
 
            // Interchanging rows and columns to get the
            // transpose of the cofactor matrix
            adj[j][i] = (sign)*(determinant(temp, N-1));
        }
    }
}
 
// Function to calculate and store inverse, returns false if
// matrix is singular
bool inverse(int A[][], float inverse[][], int N)
{
    // Find determinant of A[][]
    int det = determinant(A, N);
    if (det == 0)
    {
        cout << "Singular matrix, can't find its inverse";
        return false;
    }
 
    // Find adjoint
    int adj[N][N];
    adjoint(A, adj);
 
    // Find Inverse using formula "inverse(A) = adj(A)/det(A)"
    for (int i=0; i<N; i++) {
        for (int j=0; j<N; j++){
            inverse[i][j] = adj[i][j]/float(det);
        }
    }
 
    return true;
}

void m_multiply(float mat1[][], float mat2[][], int Row, int Col, int N, float *res[][])
{
    int i, j, k;
    for (i = 0; i < Row; i++)
    {
        for (j = 0; j < Col; j++)
        {
            res[i][j] = 0;
            for (k = 0; k < N; k++){
                res[i][j] += mat1[i][k]*mat2[k][j];
            }
        }
    }
}


void transpose(float a[][], int r, int c, float *transpose[][]){

    int i, j;

    // Finding the transpose of matrix a
    for (i=0; i<r; i++) {
        for(j=0; j<c; j++)
        {
            transpose[j][i] = a[i][j];
        }
    }
}

void m_addition (float mat1[][], float mat2[][], int r, int c, float *add[][]){
    int i, j;

    // Finding the transpose of matrix a
    for (i=0; i<r; i++) {
        for(j=0; j<c; j++)
        {
            add[i][j] = mat1[i][j] + mat2[i][j];
        }
    }
}
void m_reduction (float mat1[][], float mat2[][], int r, int c, float red[][]){
    int i, j;

    // Finding the transpose of matrix a
    for (i=0; i<r; i++) {
        for(j=0; j<c; j++)
        {
            red[i][j] = mat1[i][j] - mat2[i][j];
        }
    }
}

void inversion_2x2 (float mat[2][2], float *inv[2][2]){
    float a = mat[0][0];
    float b = mat[0][1];
    float c = mat[1][0];
    float d = mat[1][1];
    float det = 1/(a*d-b*c);
    inv[0][0] = d/det;
    inv[0][1] = -b/det;
    inv[1][0] = -c/det;
    inv[1][1] = a/det;
}

float dot_p(float v1[3][1], float v2[3][1]){
    float v = v1[1][1] * v2[1][1] + v1[2][1] * v2[2][1] + v1[3][1] * v2[3][1];
    return v;
}

void highest_three(float arr[], int arr_size, int *result[3])
{
    int i;
    float first, second, third;
  
    third = first = second = -200.0;
    for (i = 0; i < arr_size ; i ++)
    {
        /* If current element is smaller than first*/
        if (arr[i] > first)
        {
            third = second;
            second = first;
            first = arr[i];
            result[0] = i;
        }
  
        /* If arr[i] is in between first and second then update second  */
        else if (arr[i] > second)
        {
            third = second;
            second = arr[i];
            result[1] = i;
        }
  
        else if (arr[i] > third)
            third = arr[i];
            result[2] = i;
    }
  
    printf("Three largest elements are %d %d %d\n", first, second, third);
}

float RSSI_dis(int16_t RSSI){
    float A = -70.0; // calibration, RSSI at 1 meter
    float distance;
    distance = pow(10, (RSSI - A)/(-10));
    //normalize the distance
    distance = distance/normalize_factor;
    return distance
}

void calculate(float a, float b, float *x, float *y) {
    *x = 1.0;
    *y = 2.0;
}

float result_x, result_y; 

calculate(1.0, 1.0, &result_x, &result_y);

void uuid_to_coordinates(int16_t uuid, float *x, float *y){
    if (uuid == ){
        *x = 0.0;
        *y = 0.0;
    }
    else if (uuid == ) {
        *x = 1.0;
        *y = 0.0;
    }
    else if (uuid == ){
        *x = 0.0;
        *y = 1.0;
    }
    else if (uuid == ){
        *x = 1.0;
        *y = 1.0;
    }
    else {
        NRF_LOG_INFO("UUId not in list");
    }
}

float coordinates_RSSI(int16_t uuid_1, int16_t uuid_2, int16_t uuid_3, int16_t uuid_4, float *x, float *y){
    
    float RSSI_1, RSSI_2, RSSI_3, RSSI_4; // RSSI strength of three hubs

    float hub_1_x, hub_1_y, hub_2_x, hub_2_y, hub_3_x, hub_3_y; // three hubs with strongest RSSI
    float d1, d2, d3, d4;

    int RSSI_top3[3]; // the list id of the top 3 RSSI signal
    float RSSI_array[4] = [RSSI_1, RSSI_2, RSSI_3, RSSI_4];
    int16_t uuid_array[4] = [uuid_1, uuid_2, uuid_3, uuid_4];
    highest_three(RSSI_array, 3, RSSI_top3);

    uuid_to_coordinates(uuid_array[RSSI_top3[0]], float hub_1_x, float hub_1_y);
    uuid_to_coordinates(uuid_array[RSSI_top3[1]], float hub_2_x, float hub_2_y);
    uuid_to_coordinates(uuid_array[RSSI_top3[2]], float hub_3_x, float hub_3_y);
    // uuid_to_coordinates(int16_t uuid_4, float hub_4_x, float hub_4_y);

    d1 = RSSI_dis(RSSI_array[RSSI_top3[0]]);
    d2 = RSSI_dis(RSSI_array[RSSI_top3[1]]);
    d3 = RSSI_dis(RSSI_array[RSSI_top3[2]]);
    // d4 = RSSI_dis(RSSI_4);



    float A [][] = { 
        {2 * (hub_2_x - hub_1_x), 2 * (hub_2_y - hub_1_y)},
        {2 * (hub_3_x - hub_1_x), 2 * (hub_3_y - hub_1_y)}
    };
    float B [][] = {
        {d1*d1 - d2*d2 + hub_2_x*hub_2_x + hub_2_y*hub_2_y - hub_1_x*hub_1_x - hub_1_y*hub_1_y},
        {d1*d1 - d3*d3 + hub_3_x*hub_3_x + hub_3_y*hub_3_y - hub_1_x*hub_1_x - hub_1_y*hub_1_y}
    };
    // a quick way to determine x and y using three points
    float a, b, c ,d, e, f;

    a = 2 * (hub_2_x - hub_1_x);
    b = 2 * (hub_2_y - hub_1_y);
    c = 2 * (hub_3_x - hub_1_x);
    d = 2 * (hub_3_y - hub_1_y);
    e = d1*d1 - d2*d2 + hub_2_x*hub_2_x + hub_2_y*hub_2_y - hub_1_x*hub_1_x - hub_1_y*hub_1_y;
    f = d1*d1 - d3*d3 + hub_3_x*hub_3_x + hub_3_y*hub_3_y - hub_1_x*hub_1_x - hub_1_y*hub_1_y;
    *x = (b*f/d-e)/(b*c/d-a);
    *y = (e - a*x)/b;

    // int adj[2][2];  // To store adjoint of A[][]
    // float inv[2][2]; // To store inverse of A[][]
    // int det = determinant(A, 2);

    // if (det == 0)
    // {
    //     cout << "Singular matrix, can't find its inverse";
    //     return false;
    // }
 
    // // Find adjoint
    // adjoint(A, adj);
 
    // // Find Inverse using formula "inverse(A) = adj(A)/det(A)"
    // for (int i=0; i<N; i++)
    //     for (int j=0; j<N; j++)
    //         inverse[i][j] = adj[i][j]/float(det);

}

void projection_matrix (float accel_x, float accel_y, float accel_z, float yaw, float pitch, float roll, float *acc[4][1]){
    float rx[3][3] = {
        {1, 0, 0},
        {0, cos(roll), -sin(roll)},
        {0, sin(roll), cos(roll)}
    };
    float ry[3][3] = {
        {cos(pitch), 0, sin(pitch)},
        {0, 1, 0},
        {-sin(pitch), 0, cos(pitch)}
    };
    float rz[3][3] = {
        {cos(yaw), -sin(yaw),0},
        {sin(yaw), cos(yaw), 0}
        {0,0,1}
    };
    float r[3][3];
    float rz_ry[3][3];
    m_multiply(rz, ry, 3, 3, 3, &rz_ry);
    m_multiply(rz_ry, rx, 3, 3, 3, &r);
    float x_unit[3][1] = {
        {1},
        {0},
        {0}
    };
    float y_unit[3][1] = {
        {0},
        {1},
        {0}
    };
    float z_unit[3][1] = {
        {0},
        {0},
        {1}
    }
    float x_r[3][1], y_r[3][1], z_r[3][1];
    m_multiply(r, x, 3, 1, 3, &x_r);
    m_multiply(r, y, 3, 1, 3, &y_r);
    m_multiply(r, z, 3, 1, 3, &z_r);
    float acc_x;
    float acc_y;
    float x1 = dot_p(x_r, x_unit);
    float x2 = dot_p(y_r, x_unit);
    float x3 = dot_p(z_r, x_unit);
    float y1 = dot_p(x_r, y_unit);
    float y2 = dot_p(y_r, y_unit);
    float y3 = dot_p(z_r, y_unit);
    float acc_x = (x1 + x2 + x3) * accel_x;
    float acc_y = (y1 + y2 + y3) * accel_y;
    // the default setting assume that the orientation is straight up 
    acc[4][1] ={
        {0},
        {0},
        {accel_x},
        {accel_y}
    }

    acc[4][1] ={
        {0},
        {0},
        {acc_x},
        {acc_y}
    }
}

float Kalman_filter(int16_t uuid_1, int16_t uuid_2, int16_t uuid_3, int16_t uuid_4, float t, float *x, float *y){
    lsm9ds1_init(&imu, &twi);
    // reading the acceleration
    float accel_x, accel_y, accel_z;
    lsm9ds1_accel_read_all(&imu, &accel_x, &accel_y, &accel_z);
    float RSSI_x, RSSI_y;
    // reading RSSI value
    coordinates_RSSI(uuid_1, uuid_2, uuid_3, uuid_4, &RSSI_x, &RSSI_y);
    float A[4][4] = {
        {1.0, 0.0, t, 0.0},
        {0.0, 1.0, 0.0, t},
        {0.0, 0.0, 1.0, 0.0},
        {0.0, 0.0, 0.0, 1.0}
    };
    float B[4][4] = {
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, t, 0.0},
        {0.0, 0.0, 0.0, t}
    };
    float H[2][4] = {
        {1.0, 0.0, 0.0, 0.0},
        {0.0, 1.0, 0.0, 0.0}
    };
    float H_t[4][2] = {
        {1.0, 0.0},
        {0.0, 1.0},
        {0.0, 0.0},
        {0.0, 0.0}
    }
    

    // initilization
    if (initilization) {
        float previous_state[4][1] = {
            {0.0},
            {0.0},
            {0.0},
            {0.0}
        };
        float current_state[4][1] = {
            {RSSI_x},
            {RSSI_y},
            {0.0},
            {0.0}
        };
        float measurement_position[2][1] = {
            {RSSI_x},
            {RSSI_y}
        };
        // covariance of the state
        float P[4][4]= {
            {0.25, 0.25, 0.5E-4, 0.5E-4},
            {0.25, 0.25, 0.5E-4, 0.5E-4},
            {0.5E-4, 0.5E-4, 1E-8, 1E-8},
            {0.5E-4, 0.5E-4, 1E-8, 1E-8}
        }; 
        // process noise
        float Q[4][4]= {
            {1E-3, 0.0, 0.0, 0.0},
            {0.0, 1E-3, 0.0, 0.0},
            {0.0, 0.0, 1E-4, 0.0},
            {0.0, 0.0, 0.0, 1E-4}
        };
        // meausurement noise 
        float R[2][2]= {
            {1E-4, 0.0},
            {0.0, 1E-4}
        };
        initilization = false; 
    }   
    else {
        // prediction stage
        previous_state = current_state;
        float AX[4][1];
        float BU[4][1];
        float acc_proj[4][1]; 
        acc_projection(float accel_x, float accel_y, float accel_z, float &acc_proj);
        m_multiply(A, previous_state, 4, 4, 1, &AX); // position update based on previous speed
        m_multiply(B, acc_proj, 4, 4, 1, &BU); // speed change
        m_addition(AX, BU, &current_state); // x(k) = A*x(k-1) + B*u
        
        float A_t[4][4];
        transpose(A, 4, 4, A_t);
        float AP[4][4];
        float APA_t[4][4];
        m_multiply(A, P, 4, 4, 4, &AP);
        m_multiply(AP, A_t, 4, 4, 4, &APA_t);
        addition(APA_t, Q, 4, 4, &P); // p(k) = A*P*A_T + Q

        // measurement update
        float Y = {
            {RSSI_x},
            {RSSI_y}
        };
        float HX[2][1];
        m_multiply(H, current_state, 2, 1, 4, &HX);
        float V[2][1];
        m_reduction(Y, HX, 2, 1, &V); // V = Y - HX;

        float S[2][2];
        float HP[2][4];
        float HPH_t[2][2];
        m_multiply(H, P, 2, 4, 4, &HP);
        m_multiply(HP, H_t, 2, 2, 4, &HPH_t);
        m_addition(HPH_t, R, 2, 2, &S); // S = H*P*H_t + R

        float S_inv[2][2];
        inversion_2x2(S, &S_inv);
        float K[4][2];
        float PH_t[4][2];
        m_multiply(P, H_t, 4, 2, 4, &PH_t);
        m_multiply(PH_t, S_inv, 4, 2, 2, &K); // K = P * H_t * S_inv

        float KV[4][1];
        float X_new[4][1];
        m_multiply(K, V, 4, 1, 2, &KV);
        m_addition(current_state, KV, 4, 1, &X_new) // X(k) = X(k) + K*V
        current_state = X_new; 

        float K_t[2][4];
        float KSK_t[4][4];
        float P_new[4][4];
        transpose(K, 4, 2, K_t);
        float KS[4][2];
        m_multiply(K, S, 4, 2, 2, &KS);
        m_multiply(KS, K_t, 4, 4, 2, &KSK_t);
        m_reduction(P, KSK_t, 4, 4, &P_new);
        P = P_new; // P = P - K*S*K_t
    }
    
    

}


