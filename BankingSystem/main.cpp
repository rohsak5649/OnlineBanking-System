// main.cpp
#include <iostream>
#include <mysql.h>
#include <string>
#include <ctime>
#include <iomanip>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <algorithm>

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

/* ========== TRANSACTIONS & UTIL ========== */

void insert_transaction(MYSQL* conn, int customerId, const string &type, double amount, const string &desc) {
    string q = "INSERT INTO transactions (customer_id, type, amount, description) VALUES (";
    q += to_string(customerId) + ", '" + escape_string(conn, type) + "', " + to_string(amount) + ", '" + escape_string(conn, desc) + "')";
    executeQuery(conn, q);
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

/* ========== LUHN ALGO & CARD GENERATION ========== */

// Compute Luhn check digit for numeric string (without check digit)
int luhn_compute_check_digit(const string &num_no_check) {
    int sum = 0;
    bool double_it = true; // we will process from right to left, doubling every second digit
    // Start from rightmost digit of num_no_check
    for (int i = (int)num_no_check.size() - 1; i >= 0; --i) {
        int d = num_no_check[i] - '0';
        if (double_it) {
            d = d * 2;
            if (d > 9) d -= 9;
        }
        sum += d;
        double_it = !double_it;
    }
    int check = (10 - (sum % 10)) % 10;
    return check;
}

// Luhn full validation
bool luhn_validate(const string &full_number) {
    if (full_number.empty()) return false;
    int sum = 0;
    bool double_it = false; // check digit is last (rightmost) so start doubling from second-last
    for (int i = (int)full_number.size() - 1; i >= 0; --i) {
        int d = full_number[i] - '0';
        if (double_it) {
            d = d * 2;
            if (d > 9) d -= 9;
        }
        sum += d;
        double_it = !double_it;
    }
    return (sum % 10) == 0;
}

// Check if card number exists in DB (either pending or approved)
bool db_card_exists(MYSQL* conn, const string &cardNumber) {
    string q = "SELECT id FROM credit_cards WHERE card_number = '" + escape_string(conn, cardNumber) + "' LIMIT 1";
    if (mysql_query(conn, q.c_str()) != 0) return true; // on error assume exists to avoid duplicates
    MYSQL_RES* r = mysql_store_result(conn);
    bool exists = false;
    if (r) {
        exists = (mysql_num_rows(r) > 0);
        mysql_free_result(r);
    }
    return exists;
}

// Generate random numeric string of length n
string rand_numeric_str(int n) {
    string s;
    s.reserve(n);
    for (int i = 0; i < n; ++i) s.push_back(char('0' + (rand() % 10)));
    return s;
}

// Given an issuer code, generate a candidate 16-digit card number (all outputs 16 digits to match DB schema)
string generate_candidate_card(MYSQL* conn, const string &issuer) {
    // Define BIN/prefix suggestions (you can expand to more precise BIN ranges)
    string prefix;
    if (issuer == "Visa") prefix = "4";              // Visa starts with 4
    else if (issuer == "MasterCard") prefix = "5";   // MasterCard commonly 51-55 or 2221-2720; simplified to '5'
    else if (issuer == "AmEx") prefix = "37";        // AmEx starts with 34 or 37 -> using 37 (will pad to 16 digits)
    else if (issuer == "RuPay") prefix = "60";       // example domestic prefix (varies) - using 60
    else prefix = "9";                               // fallback / other

    const int total_len = 16; // our DB stores 16 chars (if you want to support AmEx 15-digit adjust DB)
    int body_len = total_len - (int)prefix.size() - 1; // minus check digit
    if (body_len <= 0) body_len = total_len - 1;

    // Try multiple times to avoid collisions
    for (int attempt = 0; attempt < 10; ++attempt) {
        string body = rand_numeric_str(body_len);
        string without_check = prefix + body;
        int check = luhn_compute_check_digit(without_check);
        string card = without_check + char('0' + check);
        if (!db_card_exists(conn, card)) return card;
    }

    // If still colliding (very unlikely), fall back to using timestamp-derived randomness and loop until unique
    int safety = 0;
    while (safety < 1000) {
        string body = to_string((unsigned long long)time(nullptr) ^ (unsigned long long)(rand()));
        // trim or pad to required length
        if ((int)body.size() < body_len) body += rand_numeric_str(body_len - (int)body.size());
        if ((int)body.size() > body_len) body = body.substr(0, body_len);
        string without_check = prefix + body;
        int check = luhn_compute_check_digit(without_check);
        string card = without_check + char('0' + check);
        if (!db_card_exists(conn, card)) return card;
        ++safety;
    }
    return ""; // failed to generate unique number (very unlikely)
}

string mask_card_number(const string &card) {
    if (card.size() < 8) return card;
    string masked = card;
    for (size_t i = 6; i + 4 < masked.size(); ++i) masked[i] = '*';
    return masked;
}

/* ========== CUSTOMER FEATURES ========== */

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
void check_credit_card_interest(MYSQL* conn, int customerId) {
    string q = "SELECT id, used_amount, last_payment_date FROM credit_cards WHERE customer_id = "
             + to_string(customerId) + " AND status = 'Approved' AND used_amount > 0";
    if (mysql_query(conn, q.c_str()) != 0) return;
    MYSQL_RES* res = mysql_store_result(conn);
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        int cardId = stoi(row[0]);
        double used = atof(row[1]);
        string lastPay = row[2] ? row[2] : "";
        if (!lastPay.empty()) {
            string diffQ = "SELECT DATEDIFF(CURDATE(), '" + string(lastPay) + "')";
            if (mysql_query(conn, diffQ.c_str()) == 0) {
                MYSQL_RES* dres = mysql_store_result(conn);
                MYSQL_ROW drow = dres ? mysql_fetch_row(dres) : nullptr;
                if (drow && atoi(drow[0]) > 45) {
                    double newAmt = used * 1.16; // add 16% interest
                    string upd = "UPDATE credit_cards SET used_amount = " + to_string(newAmt)
                               + " WHERE id = " + to_string(cardId);
                    executeQuery(conn, upd);
                    insert_transaction(conn, customerId, "Interest", newAmt - used, "16% interest applied after 45 days");
                    log_event(conn, "CreditCardInterest", "16% interest applied after 45 days", customerId);
                    cout << "⚠️  Interest applied on your credit card due to 45+ days delay.\n";
                }
                if (dres) mysql_free_result(dres);
            }
        }
    }
    if (res) mysql_free_result(res);
}


void customerMenu(MYSQL* conn, int customerId) {
    int choice;
    while (true) {
        cout << "\n===== CUSTOMER MENU =====\n1.Add Money\n2.Withdraw\n3.Check Balance\n4.Send Money\n5.Pay Bills\n6.Fund Transfer\n7.Apply Loan\n8.Apply Credit Card\n9.View Transactions\n10.Use Credit Card \n11.Logout  \nChoose: ";

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
    cout << "Enter your credit score (300-850): "; cin >> creditScore;
    cout << "Enter your monthly salary: ₹"; cin >> salary;

    if (creditScore < 400) { cout << "Not eligible (score < 400).\n"; continue; }

    // Check if existing card
    string checkQ = "SELECT id FROM credit_cards WHERE customer_id = " + to_string(customerId) + " LIMIT 1";
    if (mysql_query(conn, checkQ.c_str()) == 0) {
        MYSQL_RES* r = mysql_store_result(conn);
        if (r && mysql_num_rows(r) > 0) {
            cout << "You already have or applied for a credit card.\n";
            if (r) mysql_free_result(r);
            continue;
        }
        if (r) mysql_free_result(r);
    }

    cout << "Choose card issuer:\n1) MasterCard\n2) Visa\n3) Amex\n4) RuPay\nEnter choice [1-4]: ";
    int issuerChoice; cin >> issuerChoice;
    string issuer;
    switch (issuerChoice) {
        case 1: issuer = "MasterCard"; break;
        case 2: issuer = "Visa"; break;
        case 3: issuer = "Amex"; break;
        case 4: issuer = "RuPay"; break;
        default: issuer = "Visa"; break;
    }

    // Determine BIN prefix
    string prefix;
    if (issuer == "Visa") prefix = "4";
    else if (issuer == "MasterCard") prefix = to_string(51 + rand() % 5);
    else if (issuer == "Amex") prefix = (rand() % 2 ? "34" : "37");
    else if (issuer == "RuPay") prefix = to_string(60 + rand() % 6);
    else prefix = "9";

    string candidate = generate_candidate_card(conn, prefix);
    if (candidate.empty()) { cout << "Error generating card.\n"; continue; }

    double creditLimit = max(1000.0, salary * 3.0);
    string insertQ =
        "INSERT INTO credit_cards (customer_id, card_number, issuer, status, credit_limit, used_amount, joining_fee, last_payment_date, request_time) VALUES ("
        + to_string(customerId) + ", '" + escape_string(conn, candidate) + "', '" + issuer +
        "', 'Pending', " + to_string(creditLimit) + ", 0.00, 500.00, NULL, NOW())";

    if (executeQuery(conn, insertQ)) {
        cout << "✅ Credit card request sent to Admin A4.\n";
        cout << "Proposed Card (masked): " << mask_card_number(candidate) << "\n";
        cout << "Issuer: " << issuer << " | Limit: ₹" << fixed << setprecision(2) << creditLimit << "\n";
        log_event(conn, "CreditCardApply", "Credit card applied", customerId);
    } else {
        cout << "Failed to submit credit card application.\n";
    }
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
            check_credit_card_interest(conn, customerId); // apply overdue interest if needed
            double amount;
            cout << "Enter purchase amount: ₹"; cin >> amount;
            if (amount <= 0) { cout << "Invalid.\n"; continue; }

            string q = "SELECT id, used_amount, credit_limit FROM credit_cards WHERE customer_id = "
                     + to_string(customerId) + " AND status = 'Approved' LIMIT 1";
            if (mysql_query(conn, q.c_str()) != 0) { cout << "Error fetching card.\n"; continue; }
            MYSQL_RES* res = mysql_store_result(conn);
            MYSQL_ROW row = res ? mysql_fetch_row(res) : nullptr;
            if (!row) { cout << "No approved card found.\n"; if (res) mysql_free_result(res); continue; }
            int cardId = stoi(row[0]);
            double used = atof(row[1]), limit = atof(row[2]);
            if (used + amount > limit) {
                cout << "Exceeds credit limit. Available: ₹" << (limit - used) << "\n";
            } else {
                string upd = "UPDATE credit_cards SET used_amount = used_amount + " + to_string(amount)
                           + ", last_payment_date = CURDATE() WHERE id = " + to_string(cardId);
                if (executeQuery(conn, upd)) {
                    insert_transaction(conn, customerId, "Credit Card Spend", amount, "Purchase via credit card");
                    log_event(conn, "CreditCardUse", "Customer used credit card", customerId);
                    cout << "✅ Purchase successful. Remaining limit: ₹" << (limit - used - amount) << "\n";
                }
            }
            if (res) mysql_free_result(res);
        }

        else if (choice == 11) {
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



void manage_credit_card_requests(MYSQL* conn) {
    string q = "SELECT c.id, cust.name, c.card_number, c.issuer, c.credit_limit, c.status "
               "FROM credit_cards c "
               "JOIN customers cust ON c.customer_id = cust.id "
               "WHERE c.status = 'Pending'";
    if (mysql_query(conn, q.c_str()) != 0) {
        cout << "❌ Error fetching credit card requests: " << mysql_error(conn) << endl;
        return;
    }

    MYSQL_RES* res = mysql_store_result(conn);
    if (!res || mysql_num_rows(res) == 0) {
        cout << "✅ No pending credit card requests.\n";
        if (res) mysql_free_result(res);
        return;
    }

    MYSQL_ROW row;
    cout << "\n=== Pending Credit Card Requests ===\n";
    while ((row = mysql_fetch_row(res))) {
        int id = stoi(row[0]);
        string name = row[1];
        string card = row[2];
        string issuer = row[3];
        double limit = atof(row[4]);
        string status = row[5];
        cout << "\n-------------------------------------\n";
        cout << "Request ID: " << id << "\nCustomer: " << name
             << "\nCard: " << mask_card_number(card)
             << "\nIssuer: " << issuer
             << "\nLimit: ₹" << limit
             << "\nStatus: " << status << endl;

        cout << "\nAction (1=Approve, 2=Reject, 0=Skip): ";
        int act; cin >> act;
        if (act == 1) {
            string upd = "UPDATE credit_cards SET status='Approved', approved_by='A4' WHERE id=" + to_string(id);
            if (executeQuery(conn, upd)) {
                cout << "✅ Approved successfully.\n";
                log_event(conn, "CreditCardApproved", "Credit card approved by A4", id);
            }
        } else if (act == 2) {
            string upd = "UPDATE credit_cards SET status='Rejected', approved_by='A4' WHERE id=" + to_string(id);
            if (executeQuery(conn, upd)) {
                cout << "❌ Rejected successfully.\n";
                log_event(conn, "CreditCardRejected", "Credit card rejected by A4", id);
            }
        } else {
            cout << "Skipped.\n";
        }
    }
    mysql_free_result(res);
}

/* ========== ADMIN FEATURES ========== */

void adminMenu(MYSQL* conn, int adminId, const string &role) {
    int choice;
if (role == "A4") {
    int ch;
    do {
        cout << "\n===== ADMIN A4 MENU =====\n";
        cout << "1. View & Manage Credit Card Requests\n";
        cout << "2. Block / Unblock Existing Credit Cards\n";
        cout << "3. View All Credit Cards\n";
        cout << "4. Logout\n";
        cout << "Choose option: ";
        cin >> ch;
        switch (ch) {
            case 1:
                manage_credit_card_requests(conn);
                break;

            case 2: {
                string q = "SELECT id, cust.name, c.card_number, c.status "
                           "FROM credit_cards c "
                           "JOIN customers cust ON c.customer_id = cust.id";
                if (mysql_query(conn, q.c_str()) != 0) {
                    cout << "Error: " << mysql_error(conn) << endl;
                    break;
                }
                MYSQL_RES* res = mysql_store_result(conn);
                if (!res || mysql_num_rows(res) == 0) {
                    cout << "No cards found.\n";
                    if (res) mysql_free_result(res);
                    break;
                }
                MYSQL_ROW row;
                while ((row = mysql_fetch_row(res))) {
                    cout << "\nCard ID: " << row[0]
                         << " | Customer: " << row[1]
                         << " | Card: " << mask_card_number(row[2])
                         << " | Status: " << row[3] << endl;
                    cout << "Action (1=Block, 2=Unblock, 0=Skip): ";
                    int act; cin >> act;
                    if (act == 1) {
                        string upd = "UPDATE credit_cards SET status='Blocked' WHERE id=" + string(row[0]);
                        executeQuery(conn, upd);
                        log_event(conn, "CreditCardBlocked", "Card blocked by A4", stoi(row[0]));
                        cout << "✅ Card blocked.\n";
                    } else if (act == 2) {
                        string upd = "UPDATE credit_cards SET status='Approved' WHERE id=" + string(row[0]);
                        executeQuery(conn, upd);
                        log_event(conn, "CreditCardUnblocked", "Card unblocked by A4", stoi(row[0]));
                        cout << "✅ Card unblocked.\n";
                    } else {
                        cout << "Skipped.\n";
                    }
                }
                mysql_free_result(res);
                break;
            }

            case 3: {
                string q = "SELECT c.id, cust.name, c.card_number, c.issuer, c.credit_limit, c.status "
                           "FROM credit_cards c JOIN customers cust ON c.customer_id = cust.id";
                if (mysql_query(conn, q.c_str()) != 0) {
                    cout << "Error: " << mysql_error(conn) << endl;
                    break;
                }
                MYSQL_RES* res = mysql_store_result(conn);
                MYSQL_ROW row;
                cout << "\n=== ALL CREDIT CARDS ===\n";
                while ((row = mysql_fetch_row(res))) {
                    cout << "\nID: " << row[0]
                         << " | Customer: " << row[1]
                         << " | Card: " << mask_card_number(row[2])
                         << " | Issuer: " << row[3]
                         << " | Limit: ₹" << row[4]
                         << " | Status: " << row[5] << endl;
                }
                mysql_free_result(res);
                break;
            }

            case 4:
                cout << "Logging out...\n";
                break;

            default:
                cout << "Invalid choice.\n";
        }
    } while (ch != 4);
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
