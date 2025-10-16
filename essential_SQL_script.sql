-- create_online_banking.sql
DROP DATABASE IF EXISTS OnlineBanking;
CREATE DATABASE OnlineBanking CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE OnlineBanking;

-- customers table
CREATE TABLE customers (
    id INT AUTO_INCREMENT PRIMARY KEY,
    full_name VARCHAR(200) NOT NULL,
    phone_number CHAR(10) NOT NULL UNIQUE,
    aadhar_number CHAR(12) NOT NULL UNIQUE,
    email VARCHAR(255),
    pin CHAR(4) NOT NULL,
    balance DECIMAL(14,2) NOT NULL DEFAULT 0.00,
    login_attempts TINYINT NOT NULL DEFAULT 0,
    is_blocked TINYINT(1) NOT NULL DEFAULT 0,
    blocked_at DATETIME NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    INDEX idx_phone (phone_number),
    INDEX idx_aadhar (aadhar_number)
) ENGINE=InnoDB;

-- administrators
CREATE TABLE administrators (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(100) NOT NULL UNIQUE,
    full_name VARCHAR(200),
    pin CHAR(4) NOT NULL,
    role ENUM('A1','A2','A3','A4','A5') NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- tellers
CREATE TABLE tellers (
    id INT AUTO_INCREMENT PRIMARY KEY,
    username VARCHAR(100) NOT NULL UNIQUE,
    full_name VARCHAR(200),
    password VARCHAR(128) NOT NULL,
    created_at DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- transactions
CREATE TABLE transactions (
    id INT AUTO_INCREMENT PRIMARY KEY,
    customer_id INT,
    type VARCHAR(50) NOT NULL,
    amount DECIMAL(14,2) NOT NULL,
    description VARCHAR(512),
    date_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE SET NULL
) ENGINE=InnoDB;

-- fund_transfers log
CREATE TABLE fund_transfers (
    id INT AUTO_INCREMENT PRIMARY KEY,
    sender_id INT NOT NULL,
    receiver_account VARCHAR(64) NOT NULL,
    receiver_bank VARCHAR(255) DEFAULT NULL,
    amount DECIMAL(14,2) NOT NULL,
    date_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (sender_id) REFERENCES customers(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- loans
CREATE TABLE loans (
    id INT AUTO_INCREMENT PRIMARY KEY,
    customer_id INT NOT NULL,
    amount DECIMAL(14,2) NOT NULL,
    status ENUM('Pending','Approved','Denied') NOT NULL DEFAULT 'Pending',
    approved_by VARCHAR(10) DEFAULT NULL, -- A1/A2/A3
    request_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    approval_time DATETIME DEFAULT NULL,
    FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- credit cards
CREATE TABLE credit_cards (
    id INT AUTO_INCREMENT PRIMARY KEY,
    customer_id INT NOT NULL,
    card_number CHAR(16) NOT NULL UNIQUE,
    status ENUM('Pending','Approved','Blocked') NOT NULL DEFAULT 'Pending',
    credit_limit DECIMAL(14,2) DEFAULT 0.00,
    approved_by VARCHAR(10) DEFAULT NULL,
    approval_time DATETIME DEFAULT NULL,
    request_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- bill payments
CREATE TABLE bill_payments (
    id INT AUTO_INCREMENT PRIMARY KEY,
    customer_id INT NOT NULL,
    bill_type VARCHAR(100),
    amount DECIMAL(14,2) NOT NULL,
    date_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- logs - general system logs
CREATE TABLE logs (
    id INT AUTO_INCREMENT PRIMARY KEY,
    customer_id INT DEFAULT NULL,
    admin_id INT DEFAULT NULL,
    teller_id INT DEFAULT NULL,
    event_type VARCHAR(100),
    message VARCHAR(1024),
    event_time DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (customer_id) REFERENCES customers(id) ON DELETE SET NULL,
    FOREIGN KEY (admin_id) REFERENCES administrators(id) ON DELETE SET NULL,
    FOREIGN KEY (teller_id) REFERENCES tellers(id) ON DELETE SET NULL
) ENGINE=InnoDB;

-- sample administrator accounts (default pins - change asap)
INSERT INTO administrators (username, full_name, pin, role)
VALUES
('admin_a1','Admin A1','1111','A1'),
('admin_a2','Admin A2','1111','A2'),
('admin_a3','Admin A3','1111','A3'),
('admin_a4','Admin A4','1111','A4'),
('admin_a5','Admin A5','1111','A5');

-- sample teller
INSERT INTO tellers (username, full_name, password)
VALUES ('teller1','Teller One','tellerpass');

