// main.cpp
#include <iostream>
#include <mysql.h>
#include <string>
#include <ctime>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cstdlib>

using namespace std;

/* ========== CONFIG ========== */
const char* DB_HOST = "localhost";
const unsigned int DB_PORT = 3306;
const char* DB_USER = "root";
const char* DB_PASS = "Rohan@5649"; // <-- change / secure this
const char* DB_NAME = "OnlineBanking";

/* ========== HELPERS ========== */

void check_mysql_error(MYSQL* conn, const string &context) {
    if (mysql_errno(conn)) {
        cerr << "MySQL error (" << context << "): " << mysql_error(conn) << "\n";
    }
}

MYSQL* connectDatabase() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        cerr << "mysql_init() failed\n";
        exit(1);
    }
    if (!mysql_real_connect(conn, DB_HOST, DB_USER, DB_PASS, DB_NAME, DB_PORT, NULL, 0)) {
        cerr << "MySQL Connection Failed: " << mysql_error(conn) << endl;
        mysql_close(conn);
        exit(1);
    }
    // Ensure UTF8
    mysql_set_character_set(conn, "utf8mb4");
    return conn;
}

bool executeQuery(MYSQL* conn, const string &q) {
    if (mysql_query(conn, q.c_str()) != 0) {
        check_mysql_error(conn, "executeQuery");
        return false;
    }
    return true;
}

string escape_string(MYSQL* conn, const string &s) {
    if (!conn) return s;
    string out;
    out.resize(s.size() * 2 + 1);
    unsigned long new_len = mysql_real_escape_string(conn, &out[0], s.c_str(), (unsigned long)s.length());
    out.resize(new_len);
    return out;
}

void log_event(MYSQL* conn, const string &etype, const string &message, int customer_id = 0, int admin_id = 0, int teller_id = 0) {
    string q = "INSERT INTO logs (customer_id, admin_id, teller_id, event_type, message) VALUES (";
    q += (customer_id ? to_string(customer_id) : "NULL") + string(", ");
    q += (admin_id ? to_string(admin_id) : "NULL") + string(", ");
    q += (teller_id ? to_string(teller_id) : "NULL") + string(", ");
    q += "'" + escape_string(conn, etype) + "', ";
    q += "'" + escape_string(conn, message) + "')";
    executeQuery(conn, q);
}

/* ========== CUSTOMER FEATURES ========== */

void insert_transaction(MYSQL* conn, int customerId, const string &type, double amount, const string &desc) {
    string q = "INSERT INTO transactions (customer_id, type, amount, description) VALUES (";
    q += to_string(customerId) + ", '" + escape_string(conn, type) + "', " + to_string(amount) + ", '" + escape_string(conn, desc) + "')";
    executeQuery(conn, q);
}

void createAccount(MYSQL* conn) {
    string name, phone, aadhar, email, pin;
    double deposit;

    cin.ignore(numeric_limits<streamsize>::max(), '\n');
    cout << "Enter Full Name: "; getline(cin, name);

    // phone
    while (true) {
        cout << "Enter Phone Number (10-digit): ";
        getline(cin, phone);
        if (phone.length() != 10 || phone.find_first_not_of("0123456789") != string::npos)
            cout << "Phone must be 10 digits.\n";
        else break;
    }

    while (true) {
        cout << "Enter Aadhar (12-digit): ";
        getline(cin, aadhar);
        if (aadhar.length() != 12 || aadhar.find_first_not_of("0123456789") != string::npos)
            cout << "Aadhar must be 12 digits.\n";
        else break;
    }

    cout << "Enter Email ID: "; getline(cin, email);

    while (true) {
        cout << "Set 4-digit PIN: ";
        getline(cin, pin);
        if (pin.length() != 4 || pin.find_first_not_of("0123456789") != string::npos)
            cout << "PIN must be 4 digits.\n";
        else break;
    }

    while (true) {
        cout << "Initial Deposit (min ₹500): ₹";
        if (!(cin >> deposit)) { cin.clear(); cin.ignore(1024,'\n'); cout << "Invalid amount\n"; continue; }
        if (deposit < 500.0) cout << "Min ₹500 required.\n";
        else break;
    }

    // uniqueness check
    string checkQ = "SELECT id FROM customers WHERE phone_number = '" + escape_string(conn, phone) + "' OR aadhar_number = '" + escape_string(conn, aadhar) + "'";
    if (mysql_query(conn, checkQ.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(conn);
        if (res && mysql_num_rows(res) > 0) {
            cout << "Phone or Aadhar already exists.\n";
            if (res) mysql_free_result(res);
            return;
        }
        if (res) mysql_free_result(res);
    }

    string insertQ = "INSERT INTO customers (full_name, phone_number, aadhar_number, email, pin, balance) VALUES ('"
        + escape_string(conn, name) + "', '" + escape_string(conn, phone) + "', '" + escape_string(conn, aadhar) + "', '"
        + escape_string(conn, email) + "', '" + escape_string(conn, pin) + "', " + to_string(deposit) + ")";

    if (executeQuery(conn, insertQ)) {
        my_ulonglong id = mysql_insert_id(conn);
        cout << "Account created. Customer ID: " << id << "\n";
        insert_transaction(conn, (int)id, "Deposit", deposit, "Initial Deposit");
        log_event(conn, "CustomerCreated", "Customer account created", (int)id);
    } else {
        cout << "Account creation failed.\n";
    }
}

double get_customer_balance(MYSQL* conn, int customerId) {
    string q = "SELECT balance FROM customers WHERE id = " + to_string(customerId);
    if (mysql_query(conn, q.c_str()) != 0) return -1;
    MYSQL_RES* res = mysql_store_result(conn);
    MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
    double bal = row ? stod(row[0]) : -1;
    if (res) mysql_free_result(res);
    return bal;
}

void customerMenu(MYSQL* conn, int customerId) {
    int choice;
    while (true) {
        cout << "\n===== CUSTOMER MENU =====\n1.Add Money  2.Withdraw  3.Check Balance\n4.Send Money 5.Pay Bills 6.Fund Transfer\n7.Apply Loan 8.Apply Credit Card 9.View Transactions 10.Logout\nChoose: ";
        if (!(cin >> choice)) { cin.clear(); cin.ignore(1024,'\n'); continue; }

        if (choice == 1) {
            double amount; cout << "Amount to deposit: ₹"; cin >> amount;
            if (amount <= 0) { cout << "Invalid amount.\n"; continue; }
            string q = "UPDATE customers SET balance = balance + " + to_string(amount) + " WHERE id = " + to_string(customerId);
            if (executeQuery(conn, q)) { insert_transaction(conn, customerId, "Deposit", amount, "Money Added"); cout << "Money added.\n"; log_event(conn, "Deposit", "Customer deposited money", customerId); }
        }
        else if (choice == 2) {
            double amount; cout << "Amount to withdraw: ₹"; cin >> amount;
            double bal = get_customer_balance(conn, customerId);
            if (bal < 0) { cout << "Error reading balance.\n"; continue; }
            if (bal - amount >= 500.0) {
                executeQuery(conn, "UPDATE customers SET balance = balance - " + to_string(amount) + " WHERE id = " + to_string(customerId));
                insert_transaction(conn, customerId, "Withdraw", amount, "Cash Withdrawn");
                cout << "Withdraw successful. (Remaining masked)\n";
                log_event(conn, "Withdraw", "Customer withdrew money", customerId);
            } else {
                cout << "Must maintain minimum ₹500 after withdrawal.\n";
            }
        }
        else if (choice == 3) {
            double bal = get_customer_balance(conn, customerId);
            if (bal >= 0) cout << "Your balance: ₹" << fixed << setprecision(2) << bal << "\n";
            else cout << "Unable to fetch balance.\n";
        }
        else if (choice == 4) {
            cout << "Send Money To:\n1. Same Bank (0%) 2. Other Bank (2%)\nChoose: ";
            int type; cin >> type;
            if (type == 1) {
                int recipientId; double amount; cout << "Recipient Customer ID: "; cin >> recipientId; cout << "Amount: ₹"; cin >> amount;
                // Check existence
                string checkQ = "SELECT id FROM customers WHERE id = " + to_string(recipientId);
                mysql_query(conn, checkQ.c_str());
                MYSQL_RES* r = mysql_store_result(conn);
                MYSQL_ROW row = r ? mysql_fetch_row(r) : nullptr;
                if (!row) { cout << "Recipient not found.\n"; if (r) mysql_free_result(r); continue; }
                if (r) mysql_free_result(r);

                double bal = get_customer_balance(conn, customerId);
                if (bal >= amount && amount > 0 && bal - amount >= 500.0) {
                    executeQuery(conn, "UPDATE customers SET balance = balance - " + to_string(amount) + " WHERE id = " + to_string(customerId));
                    executeQuery(conn, "UPDATE customers SET balance = balance + " + to_string(amount) + " WHERE id = " + to_string(recipientId));
                    // log
                    executeQuery(conn, "INSERT INTO fund_transfers (sender_id, receiver_account, receiver_bank, amount) VALUES (" + to_string(customerId) + ", '" + to_string(recipientId) + "', NULL, " + to_string(amount) + ")");
                    insert_transaction(conn, customerId, "Send Money", amount, "Sent to ID: " + to_string(recipientId));
                    insert_transaction(conn, recipientId, "Received Money", amount, "Received from ID: " + to_string(customerId));
                    cout << "Sent ₹" << amount << " to ID " << recipientId << "\n";
                    log_event(conn, "FundTransfer", "Same-bank transfer", customerId);
                } else cout << "Insufficient funds or would breach minimum balance.\n";
            }
            else if (type == 2) {
                cin.ignore(numeric_limits<streamsize>::max(), '\n');
                string bankName, receiverAccount; double amount;
                cout << "Recipient Bank Name: "; getline(cin, bankName);
                cout << "Recipient Account ID: "; getline(cin, receiverAccount);
                cout << "Amount: ₹"; cin >> amount;
                double bal = get_customer_balance(conn, customerId);
                double charge = amount * 0.02;
                double total = amount + charge;
                // check daily limit
                string limitQ = "SELECT IFNULL(SUM(amount),0) FROM fund_transfers WHERE sender_id = " + to_string(customerId) + " AND receiver_bank IS NOT NULL AND DATE(date_time) = CURDATE()";
                double todayTotal = 0;
                if (mysql_query(conn, limitQ.c_str()) == 0) {
                    MYSQL_RES* r = mysql_store_result(conn);
                    MYSQL_ROW row = r ? mysql_fetch_row(r) : nullptr;
                    if (row && row[0]) todayTotal = atof(row[0]);
                    if (r) mysql_free_result(r);
                }
                if (todayTotal + amount > 10000) { cout << "Daily transfer limit ₹10,000 exceeded.\n"; continue; }
                if (bal >= total && amount > 0 && bal - total >= 500.0) {
                    executeQuery(conn, "UPDATE customers SET balance = balance - " + to_string(total) + " WHERE id = " + to_string(customerId));
                    string logFT = "INSERT INTO fund_transfers (sender_id, receiver_account, receiver_bank, amount) VALUES (" + to_string(customerId) + ", '" + escape_string(conn, receiverAccount) + "', '" + escape_string(conn, bankName) + "', " + to_string(amount) + ")";
                    executeQuery(conn, logFT);
                    insert_transaction(conn, customerId, "Send Money (Other Bank)", amount, "To " + receiverAccount + " at " + bankName);
                    cout << "Sent ₹" << amount << " to " << receiverAccount << " at " << bankName << ". Fee: ₹" << charge << " Total deducted: ₹" << total << "\n";
                    log_event(conn, "FundTransfer", "Other-bank transfer", customerId);
                } else cout << "Insufficient funds or would breach minimum balance.\n";
            } else cout << "Invalid option.\n";
        }
        else if (choice == 5) {
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            string billType; double amount;
            cout << "Bill type: "; getline(cin, billType);
            cout << "Amount: ₹"; cin >> amount;
            double bal = get_customer_balance(conn, customerId);
            if (bal - amount >= 500.0) {
                executeQuery(conn, "UPDATE customers SET balance = balance - " + to_string(amount) + " WHERE id = " + to_string(customerId));
                executeQuery(conn, "INSERT INTO bill_payments (customer_id, bill_type, amount) VALUES (" + to_string(customerId) + ", '" + escape_string(conn, billType) + "', " + to_string(amount) + ")");
                insert_transaction(conn, customerId, "Bill Payment", amount, "Bill: " + billType);
                cout << "Bill paid successfully.\n";
                log_event(conn, "BillPayment", "Bill paid", customerId);
            } else cout << "Must maintain minimum ₹500 after payment.\n";
        }
        else if (choice == 6) {
            // donation style / fund transfer to org
            cin.ignore(numeric_limits<streamsize>::max(), '\n');
            string orgName, orgAccount; double amount;
            cout << "Organization Name: "; getline(cin, orgName);
            cout << "Organization Account Number: "; getline(cin, orgAccount);
            cout << "Donation Amount: ₹"; cin >> amount;
            double bal = get_customer_balance(conn, customerId);
            if (bal >= amount && amount > 0) {
                executeQuery(conn, "UPDATE customers SET balance = balance - " + to_string(amount) + " WHERE id = " + to_string(customerId));
                executeQuery(conn, "INSERT INTO fund_transfers (sender_id, receiver_account, receiver_bank, amount) VALUES (" + to_string(customerId) + ", '" + escape_string(conn, orgAccount) + "', '" + escape_string(conn, orgName) + "', " + to_string(amount) + ")");
                insert_transaction(conn, customerId, "Donate", amount, "Donation to " + orgName);
                cout << "Donated ₹" << amount << " to " << orgName << "\n";
                log_event(conn, "Donate", "Donation made", customerId);
            } else cout << "Insufficient balance.\n";
        }
        else if (choice == 7) {
            int creditScore; double loanAmt;
            cout << "Enter your credit score: "; cin >> creditScore;
            if (creditScore < 100) { cout << "Loan rejected due to low credit score.\n"; continue; }
            cout << "Enter loan amount: ₹"; cin >> loanAmt;
            string admin;
            if (loanAmt < 20000) admin = "A3";
            else if (loanAmt <= 100000) admin = "A2";
            else admin = "A1";
            string q = "INSERT INTO loans (customer_id, amount, status, approved_by) VALUES (" + to_string(customerId) + ", " + to_string(loanAmt) + ", 'Pending', '" + admin + "')";
            if (executeQuery(conn, q)) {
                cout << "Loan request sent to Admin " << admin << "\n";
                log_event(conn, "LoanRequest", "Loan requested", customerId);
            } else cout << "Failed to submit loan request.\n";
        }
        else if (choice == 8) {
            int creditScore; double salary;
            cout << "Credit score: "; cin >> creditScore;
            cout << "Monthly salary: ₹"; cin >> salary;
            // check already applied
            string checkQ = "SELECT id FROM credit_cards WHERE customer_id = " + to_string(customerId);
            if (mysql_query(conn, checkQ.c_str()) == 0) {
                MYSQL_RES* r = mysql_store_result(conn);
                if (r && mysql_num_rows(r) > 0) { cout << "You already applied for a credit card.\n"; mysql_free_result(r); continue; }
                if (r) mysql_free_result(r);
            }
            if (creditScore < 400) { cout << "Not eligible (score < 400).\n"; continue; }
            double creditLimit = salary * 3.0;
            // card number generation (pseudo)
            unsigned long long n = ((unsigned long long)rand() << 32) ^ (unsigned long long)time(nullptr);
            n = (n % 9000000000000000ULL) + 1000000000000000ULL;
            string cardNum = to_string(n);
            string insertQ = "INSERT INTO credit_cards (customer_id, card_number, status, credit_limit, approved_by) VALUES (" + to_string(customerId) + ", '" + cardNum + "', 'Pending', " + to_string(creditLimit) + ", 'A4')";
            if (executeQuery(conn, insertQ)) {
                cout << "Credit card request sent to Admin A4. Card number: " << cardNum << " Limit: ₹" << creditLimit << "\n";
                log_event(conn, "CreditCardApply", "Credit card applied", customerId);
            } else cout << "Failed to apply for credit card.\n";
        }
        else if (choice == 9) {
            string q = "SELECT type, amount, date_time, description FROM transactions WHERE customer_id = " + to_string(customerId) + " ORDER BY date_time DESC";
            if (mysql_query(conn, q.c_str()) == 0) {
                MYSQL_RES* r = mysql_store_result(conn);
                MYSQL_ROW row;
                cout << "\n-- Transaction History --\n";
                while ((row = mysql_fetch_row(r))) {
                    cout << (row[2] ? row[2] : string("NULL")) << " | " << row[0] << " | ₹" << row[1] << " | " << (row[3] ? row[3] : string("")) << "\n";
                }
                if (r) mysql_free_result(r);
            } else cout << "Failed to fetch transactions.\n";
        }
        else if (choice == 10) {
            cout << "Logged out.\n";
            log_event(conn, "Logout", "Customer logged out", customerId);
            break;
        } else cout << "Invalid option.\n";
    }
}

/* ========== CUSTOMER LOGIN (with blocking/unblock auto) ========== */

void customerLogin(MYSQL* conn) {
    string phone, aadhar, pin;
    cout << "Phone Number: "; cin >> phone;
    cout << "Aadhar Number: "; cin >> aadhar;

    string query = "SELECT id, pin, login_attempts, is_blocked, IFNULL(blocked_at,'') FROM customers WHERE phone_number='" + escape_string(conn, phone) + "' AND aadhar_number='" + escape_string(conn, aadhar) + "'";
    if (mysql_query(conn, query.c_str()) != 0) { cout << "Query failed.\n"; return; }
    MYSQL_RES* res = mysql_store_result(conn);
    MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
    if (!row) { cout << "Customer not found.\n"; if (res) mysql_free_result(res); return; }

    int id = atoi(row[0]);
    string correctPin = row[1] ? row[1] : "";
    int attempts = row[2] ? atoi(row[2]) : 0;
    bool isBlocked = row[3] ? (atoi(row[3]) != 0) : false;
    string blockedAt = row[4] ? row[4] : "";

    // auto-unblock after 24 hours
    if (isBlocked && !blockedAt.empty()) {
        string diffQ = "SELECT TIMESTAMPDIFF(HOUR, '" + blockedAt + "', NOW())";
        if (mysql_query(conn, diffQ.c_str()) == 0) {
            MYSQL_RES* r2 = mysql_store_result(conn);
            MYSQL_ROW rr = r2 ? mysql_fetch_row(r2) : nullptr;
            int hours = rr && rr[0] ? atoi(rr[0]) : 0;
            if (r2) mysql_free_result(r2);
            if (hours >= 24) {
                executeQuery(conn, "UPDATE customers SET is_blocked = 0, login_attempts = 0, blocked_at = NULL WHERE id = " + to_string(id));
                isBlocked = false;
                attempts = 0;
                cout << "Your account was auto-unblocked after 24 hours.\n";
            }
        }
    }

    if (isBlocked) {
        cout << "Account is blocked. Contact Admin A5 to unblock.\n";
        if (res) mysql_free_result(res);
        return;
    }

    cout << "4-digit PIN: "; cin >> pin;
    if (pin == correctPin) {
        executeQuery(conn, "UPDATE customers SET login_attempts = 0 WHERE id = " + to_string(id));
        if (res) mysql_free_result(res);
        cout << "Login successful!\n";
        log_event(conn, "Login", "Customer logged in", id);
        customerMenu(conn, id);
    } else {
        attempts++;
        executeQuery(conn, "UPDATE customers SET login_attempts = " + to_string(attempts) + " WHERE id = " + to_string(id));
        if (attempts >= 3) {
            executeQuery(conn, "UPDATE customers SET is_blocked = 1, blocked_at = NOW() WHERE id = " + to_string(id));
            cout << "Account blocked after 3 failed attempts. Contact Admin A5.\n";
            log_event(conn, "Block", "Customer account blocked", id);
        } else {
            cout << "Wrong PIN. Attempt " << attempts << " of 3.\n";
        }
        if (res) mysql_free_result(res);
    }
}

/* ========== ADMIN FEATURES ========== */

void adminMenu(MYSQL* conn, int adminId, const string &role) {
    int choice;
    if (role == "A4") {
        while (true) {
            cout << "\n==== CREDIT CARD MANAGEMENT (Admin A4) ====\n1.View Pending 2.Approve/Block 3.View Customer Info 4.Logout\nChoose: ";
            cin >> choice;
            if (choice == 1) {
                string q = "SELECT id, customer_id, card_number, credit_limit FROM credit_cards WHERE status='Pending' AND approved_by='A4'";
                if (mysql_query(conn, q.c_str())==0) {
                    MYSQL_RES* r = mysql_store_result(conn);
                    MYSQL_ROW row;
                    cout << "--Pending Credit Card Requests--\n";
                    while ((row = mysql_fetch_row(r))) {
                        cout << "ReqID:" << row[0] << " CustID:" << row[1] << " Card:" << row[2] << " Limit:₹" << row[3] << "\n";
                    }
                    if (r) mysql_free_result(r);
                }
            } else if (choice == 2) {
                int reqId; char d;
                cout << "Enter Request ID: "; cin >> reqId;
                cout << "Approve or Block? (A/B): "; cin >> d;
                string status = (d=='A' || d=='a') ? "Approved" : "Blocked";
                string update = "UPDATE credit_cards SET status='" + status + "', approval_time=NOW() WHERE id=" + to_string(reqId) + " AND approved_by='A4'";
                if (executeQuery(conn, update)) cout << (status=="Approved" ? "Credit Card Approved.\n" : "Credit Card Blocked.\n");
                else cout << "Failed to update.\n";
            } else if (choice == 3) {
                int cid; cout << "Customer ID: "; cin >> cid;
                string q = "SELECT full_name, phone_number, email, balance FROM customers WHERE id=" + to_string(cid);
                if (mysql_query(conn, q.c_str())==0) {
                    MYSQL_RES* r = mysql_store_result(conn);
                    MYSQL_ROW row = r ? mysql_fetch_row(r) : nullptr;
                    if (row) cout << "Name:" << row[0] << " Phone:" << row[1] << " Email:" << row[2] << " Balance:₹" << row[3] << "\n";
                    else cout << "Customer not found.\n";
                    if (r) mysql_free_result(r);
                }
            } else if (choice == 4) { cout << "Logged out.\n"; break; }
            else cout << "Invalid.\n";
        }
    }
    else if (role == "A5") {
        while (true) {
            cout << "\n==== ACCOUNT UNBLOCKING (Admin A5) ====\n1.View Blocked 2.Unblock Customer 3.View Logs 4.Logout\nChoose: ";
            cin >> choice;
            if (choice == 1) {
                string q = "SELECT id, full_name, phone_number, blocked_at FROM customers WHERE is_blocked = 1";
                if (mysql_query(conn, q.c_str())==0) {
                    MYSQL_RES* r = mysql_store_result(conn);
                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(r))) cout << "ID:" << row[0] << " Name:" << row[1] << " Phone:" << row[2] << " BlockedAt:" << row[3] << "\n";
                    if (r) mysql_free_result(r);
                }
            } else if (choice == 2) {
                int cid; cout << "Customer ID to unblock: "; cin >> cid;
                if (executeQuery(conn, "UPDATE customers SET is_blocked=0, login_attempts=0, blocked_at=NULL WHERE id=" + to_string(cid)))
                    cout << "Customer unblocked.\n";
                else cout << "Failed to unblock.\n";
            } else if (choice == 3) {
                int custId; cout << "Cust ID for logs: "; cin >> custId;
                string q = "SELECT event_time, event_type, message FROM logs WHERE customer_id = " + to_string(custId) + " ORDER BY event_time DESC";
                if (mysql_query(conn, q.c_str())==0) {
                    MYSQL_RES* r = mysql_store_result(conn);
                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(r))) cout << row[0] << " | " << row[1] << " | " << row[2] << "\n";
                    if (r) mysql_free_result(r);
                } else cout << "Failed to retrieve logs.\n";
            } else if (choice == 4) { cout << "Logged out.\n"; break; }
            else cout << "Invalid.\n";
        }
    }
    else { // A1,A2,A3 loan managers
        while (true) {
            cout << "\n==== LOAN MANAGEMENT (Admin " << role << ") ====\n1.View Requests 2.Approve/Deny 3.View Customer Info 4.Logout\nChoose: ";
            cin >> choice;
            if (choice == 1) {
                string q = "SELECT id, customer_id, amount, status FROM loans WHERE approved_by='" + role + "' AND status='Pending'";
                if (mysql_query(conn, q.c_str())==0) {
                    MYSQL_RES* r = mysql_store_result(conn);
                    MYSQL_ROW row;
                    while ((row = mysql_fetch_row(r))) cout << "LoanID:" << row[0] << " Cust:" << row[1] << " Amount:₹" << row[2] << " Status:" << row[3] << "\n";
                    if (r) mysql_free_result(r);
                }
            } else if (choice == 2) {
                int loanId; char d; cout << "Loan ID: "; cin >> loanId; cout << "Approve or Deny? (A/D): "; cin >> d;
                string status = (d=='A' || d=='a') ? "Approved" : "Denied";
                string updateQ = "UPDATE loans SET status='" + status + "', approval_time=NOW() WHERE id=" + to_string(loanId) + " AND approved_by='" + role + "'";
                if (executeQuery(conn, updateQ)) {
                    if (status=="Approved") {
                        string fetchQ = "SELECT customer_id, amount FROM loans WHERE id=" + to_string(loanId);
                        if (mysql_query(conn, fetchQ.c_str())==0) {
                            MYSQL_RES* r = mysql_store_result(conn);
                            MYSQL_ROW row = r ? mysql_fetch_row(r) : nullptr;
                            if (row) {
                                int custId = atoi(row[0]);
                                double amount = atof(row[1]);
                                executeQuery(conn, "UPDATE customers SET balance = balance + " + to_string(amount) + " WHERE id = " + to_string(custId));
                                insert_transaction(conn, custId, "Loan Credit", amount, "Loan approved and credited");
                                cout << "Loan approved and credited to customer " << custId << "\n";
                                log_event(conn, "LoanApprove", "Loan approved", custId, adminId);
                            }
                            if (r) mysql_free_result(r);
                        }
                    } else cout << "Loan Denied.\n";
                } else cout << "Failed to process loan.\n";
            } else if (choice == 3) {
                int cid; cout << "Customer ID: "; cin >> cid;
                string q = "SELECT full_name, phone_number, email, balance FROM customers WHERE id=" + to_string(cid);
                if (mysql_query(conn, q.c_str())==0) {
                    MYSQL_RES* r = mysql_store_result(conn);
                    MYSQL_ROW row = r ? mysql_fetch_row(r) : nullptr;
                    if (row) cout << "Name:" << row[0] << " Phone:" << row[1] << " Email:" << row[2] << " Balance:₹" << row[3] << "\n";
                    else cout << "Customer not found.\n";
                    if (r) mysql_free_result(r);
                }
            } else if (choice == 4) { cout << "Logged out.\n"; break; }
            else cout << "Invalid.\n";
        }
    }
}

void adminLogin(MYSQL* conn) {
    string username, pin; cout << "Username: "; cin >> username; cout << "PIN: "; cin >> pin;
    string q = "SELECT id, role FROM administrators WHERE username='" + escape_string(conn, username) + "' AND pin='" + escape_string(conn, pin) + "'";
    if (mysql_query(conn, q.c_str()) != 0) { cout << "Query error.\n"; return; }
    MYSQL_RES* r = mysql_store_result(conn);
    MYSQL_ROW row = r ? mysql_fetch_row(r) : nullptr;
    if (!row) { cout << "Invalid username or PIN.\n"; if (r) mysql_free_result(r); return; }
    int adminId = atoi(row[0]); string role = row[1] ? row[1] : "";
    cout << "Login successful. Role: " << role << "\n";
    if (r) mysql_free_result(r);
    log_event(conn, "AdminLogin", "Admin logged in", 0, adminId);
    adminMenu(conn, adminId, role);
}

/* ========== TELLER ========== */

void tellerLogin(MYSQL* conn) {
    string user, pass; cout << "Teller Username: "; cin >> user; cout << "Teller Password: "; cin >> pass;
    string q = "SELECT id FROM tellers WHERE username='" + escape_string(conn, user) + "' AND password='" + escape_string(conn, pass) + "'";
    if (mysql_query(conn, q.c_str()) != 0) { cout << "DB query error.\n"; return; }
    MYSQL_RES* r = mysql_store_result(conn);
    MYSQL_ROW row = r ? mysql_fetch_row(r) : nullptr;
    if (!row) { cout << "Incorrect teller login.\n"; if (r) mysql_free_result(r); return; }
    int tellerId = atoi(row[0]);
    if (r) mysql_free_result(r);
    cout << "Teller login successful.\n";
    log_event(conn, "TellerLogin", "Teller logged in", 0, 0, tellerId);

    int choice;
    while (true) {
        cout << "\n==== TELLER MENU ====\n1.Process Deposit  2.Process Withdrawal  3.View Customer Info  4.Logout\nChoose: ";
        cin >> choice;
        if (choice == 1) {
            int cid; double amount; cout << "Customer ID: "; cin >> cid; cout << "Amount: ₹"; cin >> amount;
            if (executeQuery(conn, "UPDATE customers SET balance = balance + " + to_string(amount) + " WHERE id = " + to_string(cid))) {
                insert_transaction(conn, cid, "Deposit (Teller)", amount, "Teller Deposit");
                cout << "Deposit successful.\n"; log_event(conn, "TellerDeposit", "Teller processed deposit", cid, 0, tellerId);
            } else cout << "Failed to deposit.\n";
        } else if (choice == 2) {
            int cid; double amount; cout << "Customer ID: "; cin >> cid; cout << "Amount: ₹"; cin >> amount;
            double bal = get_customer_balance(conn, cid);
            if (bal - amount >= 500.0) {
                if (executeQuery(conn, "UPDATE customers SET balance = balance - " + to_string(amount) + " WHERE id = " + to_string(cid))) {
                    insert_transaction(conn, cid, "Withdraw (Teller)", amount, "Teller Withdrawal");
                    cout << "Withdrawal successful.\n"; log_event(conn, "TellerWithdraw", "Teller processed withdrawal", cid, 0, tellerId);
                } else cout << "Withdrawal failed.\n";
            } else cout << "Cannot withdraw. Minimum balance ₹500 required.\n";
        } else if (choice == 3) {
            int cid; cout << "Customer ID: "; cin >> cid;
            string qq = "SELECT full_name, phone_number, email, balance FROM customers WHERE id = " + to_string(cid);
            if (mysql_query(conn, qq.c_str())==0) {
                MYSQL_RES* r2 = mysql_store_result(conn);
                MYSQL_ROW row = r2 ? mysql_fetch_row(r2) : nullptr;
                if (row) cout << "Name:" << row[0] << " Phone:" << row[1] << " Email:" << row[2] << " Balance:₹" << row[3] << "\n";
                else cout << "Customer not found.\n";
                if (r2) mysql_free_result(r2);
            } else cout << "Query failed.\n";
        } else if (choice == 4) { cout << "Teller logged out.\n"; log_event(conn, "TellerLogout", "Teller logged out", 0, 0, tellerId); break; }
        else cout << "Invalid.\n";
    }
}

/* ========== MAIN ========= */

int main() {
    srand((unsigned)time(nullptr));
    MYSQL* conn = connectDatabase();

    int choice;
    while (true) {
        cout << "\n===== ONLINE BANKING SYSTEM =====\n1. New Customer Account\n2. Customer Login\n3. Admin Login\n4. Teller Login\n5. Exit\nChoose: ";
        if (!(cin >> choice)) { cin.clear(); cin.ignore(1024,'\n'); continue; }

        switch (choice) {
            case 1: createAccount(conn); break;
            case 2: customerLogin(conn); break;
            case 3: adminLogin(conn); break;
            case 4: tellerLogin(conn); break;
            case 5: cout << "Goodbye!\n"; mysql_close(conn); return 0;
            default: cout << "Invalid option.\n"; break;
        }
    }

    mysql_close(conn);
    return 0;
}
