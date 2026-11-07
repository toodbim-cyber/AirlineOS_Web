
#include<iostream>
#define _CRT_SECURE_NO_WARNINGS
#include "crow.h"
#include <sqlite3.h>
#include <string>
#include <sstream>
#include <mutex>
#include <random>
#include <cstdlib>
#include <stdexcept>
// Simple in-memory admin session for this local demo application.
static std::mutex g_adminMutex;
static std::string g_adminToken;

static bool isAdminAuthorized(const crow::request& req) {
    const std::string supplied = req.get_header_value("X-Admin-Token");
    if (supplied.empty()) return false;
    std::lock_guard<std::mutex> lock(g_adminMutex);
    return !g_adminToken.empty() && supplied == g_adminToken;
}

static std::string createAdminToken() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    std::ostringstream out;
    out << std::hex << dist(gen) << dist(gen);
    return out.str();
}
int main()
{
    crow::SimpleApp app;

    sqlite3* db = nullptr;
    int dbResult = sqlite3_open_v2(
      "/app/AirlineOS.db",
        &db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);


    if (dbResult != SQLITE_OK)
    {
        std::cerr << "Database connection failed: "
            << (db ? sqlite3_errmsg(db) : "Unknown SQLite error") << std::endl;
        if (db) sqlite3_close(db);
        return 1;
    }
    else
    {
        sqlite3_busy_timeout(db, 5000);
        sqlite3_exec(db, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

        // Create the database schema on first startup.
        // This is essential on a fresh cloud container where AirlineOS.db does not exist yet.
        const char* schemaSQL =
            "CREATE TABLE IF NOT EXISTS Passengers ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " name TEXT NOT NULL,"
            " phone TEXT NOT NULL,"
            " passport TEXT NOT NULL UNIQUE"
            ");"
            "CREATE TABLE IF NOT EXISTS Flights ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " flight_number TEXT NOT NULL UNIQUE,"
            " origin TEXT NOT NULL,"
            " destination TEXT NOT NULL,"
            " departure_time TEXT NOT NULL,"
            " price REAL NOT NULL CHECK(price > 0),"
            " seats_available INTEGER NOT NULL DEFAULT 0 CHECK(seats_available >= 0)"
            ");"
            "CREATE TABLE IF NOT EXISTS Bookings ("
            " id INTEGER PRIMARY KEY AUTOINCREMENT,"
            " passenger_id INTEGER NOT NULL,"
            " flight_id INTEGER NOT NULL,"
            " booking_date TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            " status TEXT NOT NULL DEFAULT 'Confirmed',"
            " seat_number TEXT,"
            " FOREIGN KEY(passenger_id) REFERENCES Passengers(id),"
            " FOREIGN KEY(flight_id) REFERENCES Flights(id)"
            ");";

        char* schemaError = nullptr;
        int schemaResult = sqlite3_exec(db, schemaSQL, nullptr, nullptr, &schemaError);
        if (schemaResult != SQLITE_OK) {
            std::cerr << "Database schema initialization failed: "
                      << (schemaError ? schemaError : sqlite3_errmsg(db)) << std::endl;
            if (schemaError) sqlite3_free(schemaError);
            sqlite3_close(db);
            return 1;
        }
        if (schemaError) sqlite3_free(schemaError);

        // Add seat_number to older databases that were created before seat selection existed.
        char* migrationError = nullptr;
        int migrationResult = sqlite3_exec(
            db, "ALTER TABLE Bookings ADD COLUMN seat_number TEXT;",
            nullptr, nullptr, &migrationError);
        if (migrationResult != SQLITE_OK && migrationError) {
            // SQLITE_ERROR with "duplicate column name" is expected on later startups.
            std::string migrationMessage = migrationError;
            if (migrationMessage.find("duplicate column name") == std::string::npos) {
                std::cerr << "Database migration failed: " << migrationMessage << std::endl;
                sqlite3_free(migrationError);
                sqlite3_close(db);
                return 1;
            }
        }
        if (migrationError) sqlite3_free(migrationError);

        const char* indexSQL =
            "CREATE UNIQUE INDEX IF NOT EXISTS UX_Bookings_Flight_Seat "
            "ON Bookings(flight_id, seat_number) "
            "WHERE status='Confirmed' AND seat_number IS NOT NULL;";
        char* indexError = nullptr;
        if (sqlite3_exec(db, indexSQL, nullptr, nullptr, &indexError) != SQLITE_OK) {
            std::cerr << "Could not create seat index: "
                      << (indexError ? indexError : sqlite3_errmsg(db)) << std::endl;
            if (indexError) sqlite3_free(indexError);
            sqlite3_close(db);
            return 1;
        }
        if (indexError) sqlite3_free(indexError);

        // Add sample flights only when they are not already present.
        const char* seedSQL =
            "INSERT OR IGNORE INTO Flights "
            "(flight_number, origin, destination, departure_time, price, seats_available) VALUES "
            "('F101','Baghdad','Erbil','2026-12-10 09:00',75,50),"
            "('F102','Erbil','Baghdad','2026-12-11 12:00',80,45),"
            "('F103','Baghdad','Istanbul','2026-12-12 15:00',250,60),"
            "('F104','Erbil','Istanbul','2026-12-13 10:00',220,40);";
        char* seedError = nullptr;
        if (sqlite3_exec(db, seedSQL, nullptr, nullptr, &seedError) != SQLITE_OK) {
            std::cerr << "Could not initialize sample flights: "
                      << (seedError ? seedError : sqlite3_errmsg(db)) << std::endl;
            if (seedError) sqlite3_free(seedError);
            sqlite3_close(db);
            return 1;
        }
        if (seedError) sqlite3_free(seedError);

        std::cout << "SQLite database connected and initialized successfully!" << std::endl;
    }

    // AirlineOS Home Page
    CROW_ROUTE(app, "/")([]()
        {
            const char* html = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>AirlineOS | Explore the World</title>

    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap" rel="stylesheet">

    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Inter', sans-serif;
            background: #f5f8fc;
            color: #14213d;
        }

        header {
            background: white;
            padding: 20px 7%;
            display: flex;
            justify-content: space-between;
            align-items: center;
            box-shadow: 0 3px 15px rgba(0,0,0,0.04);
        }

        .logo {
            font-size: 25px;
            font-weight: 800;
            color: #1261d6;
        }

        .logo span {
            color: #14213d;
        }

        nav {
            display: flex;
            gap: 30px;
            align-items: center;
        }

        nav a {
            text-decoration: none;
            color: #53627a;
            font-size: 14px;
            font-weight: 600;
        }

        nav a:hover {
            color: #1261d6;
        }

        .login-btn {
            background: #1261d6;
            color: white;
            padding: 12px 24px;
            border-radius: 9px;
        }

        .hero {
            min-height: 440px;
            padding: 85px 8%;
            color: white;
            background:
                linear-gradient(90deg, rgba(5,24,58,.92), rgba(5,24,58,.42)),
                url('https://images.unsplash.com/photo-1436491865332-7a61a109cc05?auto=format&fit=crop&w=1800&q=85');
            background-size: cover;
            background-position: center;
        }

        .hero h1 {
            font-size: 48px;
            line-height: 1.2;
            max-width: 650px;
            margin-bottom: 20px;
        }

        .hero p {
            color: #e1e9f6;
            font-size: 17px;
            margin-bottom: 30px;
        }

        .hero-label {
            color: #80b6ff;
            font-weight: 700;
            letter-spacing: 2px;
            font-size: 13px;
            margin-bottom: 18px;
        }

        .search-section {
            max-width: 1120px;
            margin: -55px auto 60px;
            padding: 0 20px;
            position: relative;
        }

        .search-box {
            background: white;
            padding: 30px;
            border-radius: 16px;
            box-shadow: 0 12px 40px rgba(20,40,80,.12);
        }

        .search-title {
            font-size: 21px;
            font-weight: 800;
            margin-bottom: 25px;
        }

        .search-grid {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 15px;
        }

        .field label {
            display: block;
            font-size: 12px;
            font-weight: 700;
            color: #65738a;
            margin-bottom: 9px;
        }

        .field input,
        .field select {
            width: 100%;
            padding: 14px;
            border: 1px solid #dce4ef;
            border-radius: 8px;
            background: #fff;
            font-size: 14px;
            color: #14213d;
            outline: none;
        }

        .field input:focus,
        .field select:focus {
            border-color: #1261d6;
        }

        .search-btn {
            width: 100%;
            margin-top: 22px;
            padding: 16px;
            border: none;
            border-radius: 9px;
            background: #1261d6;
            color: white;
            font-size: 15px;
            font-weight: 700;
            cursor: pointer;
            transition: .2s;
        }

        .search-btn:hover {
            background: #084cae;
        }

        .container {
            max-width: 1120px;
            margin: auto;
            padding: 0 20px 60px;
        }

        .section-heading {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 25px;
        }

        .section-heading h2 {
            font-size: 27px;
            font-weight: 800;
        }

        .section-heading p {
            color: #77849a;
            font-size: 13px;
            margin-top: 7px;
        }

        .routes {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 22px;
        }

        .route-card {
            background: white;
            border-radius: 14px;
            overflow: hidden;
            box-shadow: 0 5px 20px rgba(20,40,80,.05);
            transition: .25s;
        }

        .route-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 12px 28px rgba(20,40,80,.12);
        }

        .route-image {
            height: 170px;
            background-size: cover;
            background-position: center;
        }

        .baghdad {
            background-image: url('https://images.unsplash.com/photo-1565008576549-57569a49371d?auto=format&fit=crop&w=800&q=80');
        }

        .istanbul {
            background-image: url('https://images.unsplash.com/photo-1524231757912-21f4fe3a7200?auto=format&fit=crop&w=800&q=80');
        }

        .erbil {
            background-image: url('https://images.unsplash.com/photo-1516483638261-f4dbaf036963?auto=format&fit=crop&w=800&q=80');
        }

        .route-info {
            padding: 20px;
        }

        .route-info h3 {
            font-size: 18px;
            margin-bottom: 9px;
        }

        .route-info p {
            color: #7a879a;
            font-size: 13px;
        }

        .route-bottom {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-top: 20px;
            padding-top: 15px;
            border-top: 1px solid #edf0f5;
        }

        .price {
            font-size: 21px;
            font-weight: 800;
            color: #1261d6;
        }

        .price small {
            font-size: 11px;
            color: #8792a5;
            font-weight: 500;
        }

        .book-link {
            background: #eaf2ff;
            color: #1261d6;
            padding: 10px 15px;
            border-radius: 7px;
            text-decoration: none;
            font-size: 12px;
            font-weight: 700;
        }

        .features {
            display: grid;
            grid-template-columns: repeat(3, 1fr);
            gap: 20px;
            margin-top: 65px;
        }

        .feature {
            background: white;
            padding: 25px;
            border-radius: 12px;
            text-align: center;
        }

        .feature .icon {
            font-size: 30px;
            margin-bottom: 12px;
        }

        .feature h3 {
            font-size: 16px;
            margin-bottom: 8px;
        }

        .feature p {
            color: #7a879a;
            font-size: 13px;
            line-height: 1.6;
        }

        footer {
            background: #101f3b;
            color: white;
            padding: 35px 8%;
            text-align: center;
        }

        footer p {
            color: #aab7ca;
            font-size: 13px;
            margin-top: 10px;
        }

        #message {
            margin-top: 15px;
            font-size: 14px;
            color: #1261d6;
            font-weight: 600;
        }

        @media (max-width: 800px) {
            nav {
                gap: 12px;
            }

            nav a {
                font-size: 12px;
            }

            .hero h1 {
                font-size: 35px;
            }

            .search-grid {
                grid-template-columns: repeat(2, 1fr);
            }

            .routes {
                grid-template-columns: 1fr 1fr;
            }
        }

        @media (max-width: 550px) {
            header {
                flex-direction: column;
                gap: 18px;
            }

            nav {
                flex-wrap: wrap;
                justify-content: center;
            }

            .hero {
                padding: 65px 7%;
            }

            .hero h1 {
                font-size: 30px;
            }

            .search-grid,
            .routes,
            .features {
                grid-template-columns: 1fr;
            }

            .section-heading h2 {
                font-size: 21px;
            }
        }
    </style>
</head>

<body>

<header>
    <div class="logo">Airline<span>OS</span></div>

    <nav>
        <a href="/">Home</a>
        <a href="#flights">Flights</a>
        <a href="#services">Services</a>
   <a href="#my-bookings">My Bookings</a>  
   <a href="#footer">Contact</a>
        <a href="#search" class="login-btn">Book Now</a>
<a href="#admin-login" id="admin-login" class="login-btn">Admin Login</a>
    </nav>
</header>

<section class="hero">
    <div class="hero-label">YOUR JOURNEY STARTS HERE</div>

    <h1>Discover the World.<br>Fly Beyond Limits.</h1>

    <p>Find your next destination and enjoy a comfortable journey with AirlineOS.</p>
</section>

<section class="search-section" id="search">
    <div class="search-box">

        <div class="search-title">Find Your Flight</div>

        <form id="flightForm">

            <div class="search-grid">

                <div class="field">
                    <label>FROM</label>
                    <select id="origin" required>
                        <option value="">Select origin</option>
                        <option value="Baghdad">Baghdad</option>
                        <option value="Erbil">Erbil</option>
                        <option value="Istanbul">Istanbul</option>
                    </select>
                </div>

                <div class="field">
                    <label>TO</label>
                    <select id="destination" required>
                        <option value="">Select destination</option>
                        <option value="Baghdad">Baghdad</option>
                        <option value="Erbil">Erbil</option>
                        <option value="Istanbul">Istanbul</option>
                    </select>
                </div>

                <div class="field">
                    <label>DEPARTURE DATE</label>
                    <input type="date" id="departure" required>
                </div>

                <div class="field">
                    <label>PASSENGERS</label>
                    <select id="passengers">
                        <option value="1">1 Passenger</option>
                        <option value="2">2 Passengers</option>
                        <option value="3">3 Passengers</option>
                        <option value="4">4 Passengers</option>
                        <option value="5">5 Passengers</option>
                    </select>
                </div>

            </div>

            <button type="submit" class="search-btn">
                Search Flights
            </button>

            <div id="message"></div>

        </form>
    </div>
</section>

<main class="container" id="flights">

    <div class="section-heading">
        <div>
            <h2>Popular Destinations</h2>
            <p>Explore some of our popular travel destinations.</p>
        </div>
    </div>

    <div class="routes">

        <div class="route-card">
            <div class="route-image baghdad"></div>
            <div class="route-info">
                <h3>Baghdad → Erbil</h3>
                <p>Discover the beauty of northern Iraq.</p>
                <div class="route-bottom">
                    <div class="price">$75 <small>/ person</small></div>
                    <a href="#search" class="book-link">Book Now</a>
                </div>
            </div>
        </div>

        <div class="route-card">
            <div class="route-image istanbul"></div>
            <div class="route-info">
                <h3>Baghdad → Istanbul</h3>
                <p>Experience a wonderful international journey.</p>
                <div class="route-bottom">
                    <div class="price">$250 <small>/ person</small></div>
                    <a href="#search" class="book-link">Book Now</a>
                </div>
            </div>
        </div>

        <div class="route-card">
            <div class="route-image erbil"></div>
            <div class="route-info">
                <h3>Erbil → Istanbul</h3>
                <p>Explore one of the world's most beautiful cities.</p>
                <div class="route-bottom">
                    <div class="price">$220 <small>/ person</small></div>
                    <a href="#search" class="book-link">Book Now</a>
                </div>
            </div>
        </div>

    </div>

    <section class="features" id="services">

        <div class="feature">
            <div class="icon">✈️</div>
            <h3>Easy Booking</h3>
            <p>Find and book your flights through a simple experience.</p>
        </div>

        <div class="feature">
            <div class="icon">🛡️</div>
            <h3>Reliable Service</h3>
            <p>Enjoy a smooth and organized travel experience.</p>
        </div>

        <div class="feature">
            <div class="icon">🌍</div>
            <h3>Explore More</h3>
            <p>Discover destinations and plan your next journey.</p>
        </div>

    </section>

<section class="search-section" id="my-bookings">
    <div class="search-box">
        <div class="search-title">My Bookings</div>

        <p>Enter your passport number to find your bookings.</p>

        <div class="field">
            <label for="bookingPassportSearch">Passport Number</label>
            <input
                type="text"
                id="bookingPassportSearch"
                placeholder="Enter your passport number">
        </div>

        <button type="button" id="findBookingsBtn">
            Find My Bookings
        </button>

        <div id="myBookingsResults"></div>
    </div>
</section>

</main>

<footer id="footer">
    <div class="logo">AirlineOS</div>
    <p>© 2026 AirlineOS. All rights reserved.</p>
    <p>Your Journey, Our Priority.</p>
</footer>

<!-- Admin Login Modal -->
<div id="adminLoginModal"
     style="display:none; position:fixed; inset:0;
     background:rgba(0,0,0,0.65); z-index:9999;
     justify-content:center; align-items:center; padding:20px;">

    <div style="background:white; width:100%; max-width:400px;
                padding:30px; border-radius:15px;">

        <h2 style="color:#14213d;">Admin Login</h2>

        <form id="adminLoginForm">
            <label>Username</label>
            <input type="text" id="adminUsername"
                   required placeholder="Enter username"
                   style="width:100%; padding:12px; margin:10px 0;">

            <label>Password</label>
            <input type="password" id="adminPassword"
                   required placeholder="Enter password"
                   style="width:100%; padding:12px; margin:10px 0;">

            <button type="submit"
                    style="width:100%; padding:12px; background:#14213d;
                    color:white; border:0; border-radius:6px;">
                Login
            </button>

            <button type="button" id="closeAdminLogin"
                    style="width:100%; padding:10px; margin-top:10px;">
                Close
            </button>

            <p id="adminLoginMessage"></p>
        </form>
    </div>
</div>

<!-- Passenger Booking Modal -->
<div id="bookingModal" class="modal"
     style="display:none; position:fixed; inset:0;
     background:rgba(0,0,0,0.65); z-index:9999;
     justify-content:center; align-items:center; padding:20px;">

    <div style="background:white; width:100%; max-width:500px;
                padding:30px; border-radius:16px;
                box-shadow:0 10px 40px rgba(0,0,0,0.25);">

        <h2 style="color:#14213d; margin-bottom:10px;">
            Complete Your Booking
        </h2>

        <p id="selectedFlight"
           style="color:#64748b; margin-bottom:20px;">
            Selected Flight
        </p>

        <form id="bookingForm">

            <label>Full Name</label>
            <input type="text" id="bookingPassengerName"
                   required placeholder="Enter your full name"
                   style="width:100%; padding:12px; margin:8px 0 16px;
                   border:1px solid #ddd; border-radius:8px;">

            <label>Phone Number</label>
            <input type="tel" id="bookingPassengerPhone"
                   required placeholder="Enter your phone number"
                   style="width:100%; padding:12px; margin:8px 0 16px;
                   border:1px solid #ddd; border-radius:8px;">

            <label>Passport Number</label>
            <input type="text" id="bookingPassportNumber"
                   required placeholder="Enter your passport number"
                   style="width:100%; padding:12px; margin:8px 0 20px;
                   border:1px solid #ddd; border-radius:8px;">

            <label style="display:block;margin:4px 0 8px;">Choose Seat</label>
            <div id="seatMap" style="display:grid;grid-template-columns:repeat(6,minmax(35px,1fr));gap:7px;margin:10px 0 18px;"></div>
            <input type="hidden" id="bookingSeatNumber" required>

            <button type="submit"
                    style="width:100%; padding:14px;
                    background:#1764d8; color:white;
                    border:0; border-radius:8px;
                    font-weight:bold; cursor:pointer;">
                Confirm Booking
            </button>

            <button type="button" onclick="closeBooking()"
                    style="width:100%; padding:12px;
                    margin-top:10px; background:#eee;
                    border:0; border-radius:8px; cursor:pointer;">
                Cancel
            </button>

        </form>

        <div id="bookingResult"
             style="margin-top:15px; font-weight:bold;">
        </div>

    </div>
</div>

<script src="https://cdn.jsdelivr.net/npm/jspdf@2.5.1/dist/jspdf.umd.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<script>
    document.getElementById("flightForm").addEventListener("submit", async function(event) {
    event.preventDefault();

    const origin = document.getElementById("origin").value;
    const destination = document.getElementById("destination").value;
    const date = document.getElementById("departure").value;
    const message = document.getElementById("message");

    if (!origin || !destination || !date) {
        message.textContent = "Please select origin, destination and departure date.";
        return;
    }

    if (origin === destination) {
        message.textContent = "Origin and destination must be different.";
        return;
    }

    message.textContent = "Searching flights...";

    try {
        const response = await fetch(
            `/api/flights?origin=${encodeURIComponent(origin)}&destination=${encodeURIComponent(destination)}&date=${encodeURIComponent(date)}`
        );

        const flights = await response.json();

        if (flights.length === 0) {
            message.textContent = "No flights found for this route and date.";
            return;
        }

message.innerHTML = flights.map(flight => `
    <div style="background:white; padding:20px; margin-top:15px;
                border-radius:12px; box-shadow:0 3px 12px rgba(0,0,0,0.08);">

        <h3>${flight.flight_number}</h3>

        <p><strong>${flight.origin}</strong> → <strong>${flight.destination}</strong></p>

        <p>Departure: ${flight.departure_time}</p>

        <p>Price: $${flight.price}</p>

        <p>Available seats: ${flight.seats_available}</p>

        <button
            onclick="startBooking('${flight.flight_number}')"
            style="background:#1764d8; color:white; border:0;
                   padding:12px 25px; border-radius:8px;
                   cursor:pointer; font-weight:bold;">
            Book Now
        </button>
    </div>
`).join("");       

    }
    catch (error) {
        message.textContent = "Could not connect to the flight service.";
    }
});

    document.getElementById("departure").min =
        new Date().toISOString().split("T")[0];

let selectedFlightNumber = "";

async function startBooking(flightNumber) {
    selectedFlightNumber = flightNumber;
    document.getElementById("bookingSeatNumber").value = "";
    document.getElementById("selectedFlight").textContent = "Selected flight: " + flightNumber;
    document.getElementById("bookingResult").textContent = "";
    document.getElementById("bookingModal").style.display = "flex";
    const map = document.getElementById("seatMap");
    map.textContent = "Loading seats...";
    try {
        const response = await fetch(`/api/seats?flight_number=${encodeURIComponent(flightNumber)}`);
        const data = await response.json();
        if (!data.success) throw new Error(data.message || "Could not load seats");
        map.innerHTML = "";
        for (let row = 1; row <= 10; row++) {
            for (let col = 0; col < 6; col++) {
                const seat = `${row}${String.fromCharCode(65 + col)}`;
                const button = document.createElement("button");
                button.type = "button";
                button.textContent = seat;
                button.style.cssText = "padding:9px 3px;border:0;border-radius:6px;font-size:12px;";
                if (data.booked.includes(seat)) {
                    button.disabled = true; button.style.background = "#dc3545";
                    button.style.color = "white"; button.title = "Already booked";
                } else {
                    button.style.background = "#dbeafe"; button.style.cursor = "pointer";
                    button.onclick = () => {
                        document.querySelectorAll("#seatMap button").forEach(b => {
                            if (!b.disabled) { b.style.background = "#dbeafe"; b.style.color = "#14213d"; }
                        });
                        button.style.background = "#16a34a"; button.style.color = "white";
                        document.getElementById("bookingSeatNumber").value = seat;
                    };
                }
                map.appendChild(button);
            }
        }
        const legend = document.createElement("p");
        legend.style.cssText = "grid-column:1/-1;font-size:12px;color:#64748b;";
        legend.textContent = "Blue: Available | Green: Selected | Red: Booked";
        map.appendChild(legend);
    } catch (error) { map.textContent = "Unable to load seats. Please try again."; console.error(error); }
}

function closeBooking() {
    document.getElementById("bookingModal").style.display = "none";
}

async function submitBooking() {
console.log("Confirm Booking button clicked!");

   const name = document.getElementById("bookingPassengerName").value.trim();
const phone = document.getElementById("bookingPassengerPhone").value.trim();
const passport = document.getElementById("bookingPassportNumber").value.trim();
    const seatNumber = document.getElementById("bookingSeatNumber").value;

    if (!name || !phone || !passport || !seatNumber) {
        alert("Please fill in all fields and select a seat.");
        return;
    }

    if (!selectedFlightNumber) {
        alert("Please select a flight first.");
        return;
    }

    try {
        const response = await fetch("/api/book", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                flight_number: selectedFlightNumber,
                name: name,
                phone: phone,
                passport: passport,
                seat_number: seatNumber
            })
        });

        const result = await response.json();

        if (!result.success) {
            alert(result.message || "Booking failed.");
            return;
        }

        alert("Booking completed successfully!");
        closeBooking();
        location.reload();

    } catch (error) {
        alert("Could not connect to the booking service.");
        console.error(error);
    }
}

document.getElementById("bookingForm").addEventListener("submit", async function(event) {
    event.preventDefault();
    await submitBooking();
});

document.getElementById("findBookingsBtn").addEventListener("click", async function () {
    const passport = document.getElementById("bookingPassportSearch").value.trim();
    const results = document.getElementById("myBookingsResults");

    if (!passport) {
        alert("Please enter your passport number.");
        return;
    }

    results.innerHTML = "<p>Searching for your bookings...</p>";

    try {
        const response = await fetch(
            `/api/bookings?passport=${encodeURIComponent(passport)}`
        );

        const data = await response.json();

        if (!data.success) {
            results.innerHTML = "<p>Could not retrieve bookings.</p>";
            return;
        }

        if (!data.bookings || data.bookings.length === 0) {
            results.innerHTML = "<p>No bookings found for this passport number.</p>";
            return;
        }

        window.currentBookingData = Object.fromEntries(data.bookings.map(b => [b.id, b]));
        results.innerHTML = data.bookings.map(booking => `
            <div style="background:white; padding:20px; margin-top:15px;
                        border-radius:10px; box-shadow:0 2px 8px #ddd;">
                <h3>Booking #${booking.id}</h3>
                <p><strong>Flight:</strong> ${booking.flight_number}</p>
                <p><strong>From:</strong> ${booking.origin}</p>
                <p><strong>To:</strong> ${booking.destination}</p>
                <p><strong>Departure:</strong> ${booking.departure_time}</p>
                <p><strong>Price:</strong> $${booking.price}</p>
                <p><strong>Status:</strong> ${booking.status}</p>
                <p><strong>Seat:</strong> ${booking.seat_number || "Not assigned"}</p>
                <p><strong>Booking Date:</strong> ${booking.booking_date}</p>
                ${booking.status === "Confirmed" ? `<button onclick="downloadTicketById(${booking.id})" style="background:#1261d6;color:white;padding:10px 18px;border:0;border-radius:6px;cursor:pointer;margin-top:10px;">Download PDF Ticket</button>` : ""}

${booking.status === "Confirmed"
    ? `<button onclick="cancelBooking(${booking.id})"
       style="background:#dc3545;color:white;padding:10px 18px;
       border:0;border-radius:6px;cursor:pointer;margin-top:10px;">
       Cancel Booking
       </button>`
    : ""}
            </div>
        `).join("");

    } catch (error) {
        console.error(error);
        results.innerHTML = "<p>Connection error. Please try again.</p>";
    }
});
function downloadTicketById(bookingId) {
    const booking = window.currentBookingData && window.currentBookingData[bookingId];
    if (booking) downloadTicket(booking);
}

function downloadTicket(booking) {
    if (!window.jspdf || !window.jspdf.jsPDF) {
        alert("PDF library could not load. Check your internet connection."); return;
    }
    const { jsPDF } = window.jspdf;
    const doc = new jsPDF();
    doc.setFontSize(22); doc.setTextColor(18, 97, 214);
    doc.text("AirlineOS - E-Ticket", 20, 25);
    doc.setTextColor(20, 33, 61); doc.setFontSize(12);
    const rows = [
        ["Booking ID", booking.id], ["Flight", booking.flight_number],
        ["From", booking.origin], ["To", booking.destination],
        ["Departure", booking.departure_time], ["Seat", booking.seat_number || "Not assigned"],
        ["Price", "$" + booking.price], ["Status", booking.status],
        ["Booking Date", booking.booking_date]
    ];
    let y = 45;
    rows.forEach(([label, value]) => {
        doc.setFont(undefined, "bold"); doc.text(label + ":", 20, y);
        doc.setFont(undefined, "normal"); doc.text(String(value ?? ""), 65, y); y += 12;
    });
    doc.setFontSize(9);
    doc.text("Bring your passport and arrive at the airport early.", 20, y + 12);
    doc.save(`AirlineOS_Ticket_${booking.id}.pdf`);
}

async function cancelBooking(bookingId) {
    if (!confirm("Are you sure you want to cancel this booking?")) {
        return;
    }

    try {
        const response = await fetch("/api/cancel", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                booking_id: bookingId
            })
        });

        const data = await response.json();

        if (data.success) {
            alert("Booking cancelled successfully!");
            document.getElementById("findBookingsBtn").click();
        } else {
            alert("Cancellation failed: " + data.message);
        }
    } catch (error) {
        console.error(error);
        alert("Connection error. Please try again.");
    }
}

// Admin Login Window
document.getElementById("admin-login").addEventListener("click", function(event) {
    event.preventDefault();
    document.getElementById("adminLoginModal").style.display = "flex";
});

document.getElementById("closeAdminLogin").addEventListener("click", function() {
    document.getElementById("adminLoginModal").style.display = "none";
});

// Admin Login Form
document.getElementById("adminLoginForm").addEventListener("submit", async function(event) {
    event.preventDefault();

    const username = document.getElementById("adminUsername").value.trim();
    const password = document.getElementById("adminPassword").value;
    const message = document.getElementById("adminLoginMessage");

    message.textContent = "Checking login...";
    message.style.color = "blue";

    try {
        const response = await fetch("/api/admin/login", {
            method: "POST",
            headers: {
                "Content-Type": "application/json"
            },
            body: JSON.stringify({
                username: username,
                password: password
            })
        });

        const data = await response.json();

        if (data.success) {
            adminToken = data.token || "";
            sessionStorage.setItem("airlineos_admin_token", adminToken);
            message.textContent = "Login successful!";
            message.style.color = "green";

            alert("Welcome, Admin!");

            document.getElementById("adminLoginModal").style.display = "none";
const dashboard = document.createElement("div");
dashboard.id = "adminDashboard";

dashboard.style.cssText = `
    position: fixed;
    inset: 0;
    z-index: 10000;
    background: #f1f5f9;
    padding: 40px;
    overflow: auto;
    font-family: Arial;
`;

dashboard.innerHTML = `
    <h1 style="color:#14213d;">AirlineOS - Admin Dashboard</h1>

    <h2>Welcome, Admin!</h2>
<div id="adminStats" style="display:grid; grid-template-columns:repeat(auto-fit,minmax(180px,1fr)); gap:20px; margin-top:25px;">
    <div style="background:white; padding:25px; border-radius:12px;">
        <h3>Total Passengers</h3>
        <h2 id="totalPassengers">Loading...</h2>
    </div>

    <div style="background:white; padding:25px; border-radius:12px;">
        <h3>Total Flights</h3>
        <h2 id="totalFlights">Loading...</h2>
    </div>

    <div style="background:white; padding:25px; border-radius:12px;">
        <h3>Confirmed Bookings</h3>
        <h2 id="confirmedBookings">Loading...</h2>
    </div>

    <div style="background:white; padding:25px; border-radius:12px;">
        <h3>Cancelled Bookings</h3>
        <h2 id="cancelledBookings">Loading...</h2>
    </div>

    <div style="background:white; padding:25px; border-radius:12px;">
        <h3>Total Revenue</h3>
        <h2 id="totalRevenue">Loading...</h2>
    </div>
</div>
    <div style="background:white;padding:24px;border-radius:12px;margin-top:20px;">
        <h3 style="margin-bottom:12px;">Booking Overview</h3>
        <canvas id="adminBookingsChart" height="100"></canvas>
    </div>
    <div id="manageFlightsCard"
     style="background:white; padding:30px; border-radius:12px; cursor:pointer;"
     onclick="showAdminFlights()">
    <h3>Manage Flights</h3>
    <p>View and manage airline flights.</p>
</div>

        <div style="background:white; padding:30px; border-radius:12px; cursor:pointer;"
             onclick="showAdminBookings()">
            <h3>Manage Bookings</h3>
            <p>View passenger reservations.</p>
        </div>

        <div style="background:white; padding:30px; border-radius:12px; cursor:pointer;"
             onclick="showAdminPassengers()">
            <h3>Manage Passengers</h3>
            <p>View passenger information.</p>
        </div>
    </div>

    <button id="adminLogoutButton"
        style="margin-top:30px; padding:12px 30px;
        background:#dc2626; color:white; border:0;
        border-radius:6px; cursor:pointer;">
        Logout
    </button>
`;

document.body.appendChild(dashboard);
// Load Admin Statistics
async function loadAdminStats() {
    try {
        const response = await fetch("/api/admin/stats", {
            headers: { "X-Admin-Token": adminToken }
        });
        const data = await response.json();

        if (data.success) {
            document.getElementById("totalPassengers").textContent =
                data.total_passengers;

            document.getElementById("totalFlights").textContent =
                data.total_flights;

            document.getElementById("confirmedBookings").textContent =
                data.confirmed_bookings;

            document.getElementById("cancelledBookings").textContent =
                data.cancelled_bookings;

            document.getElementById("totalRevenue").textContent =
                "$" + data.total_revenue;
            const chartCanvas = document.getElementById("adminBookingsChart");
            if (chartCanvas && window.Chart) {
                if (window.airlineAdminChart) window.airlineAdminChart.destroy();
                window.airlineAdminChart = new Chart(chartCanvas, {
                    type: "bar",
                    data: {
                        labels: ["Confirmed", "Cancelled"],
                        datasets: [{ label: "Bookings", data: [data.confirmed_bookings, data.cancelled_bookings] }]
                    },
                    options: { responsive: true, plugins: { legend: { display: false } } }
                });
            }
        }
    }
    catch (error) {
        console.error("Could not load admin statistics:", error);
    }
}

loadAdminStats();
// Show and Manage Flights
window.showAdminFlights = async function() {
    let oldSection = document.getElementById("adminFlightsSection");

    if (oldSection) {
        oldSection.remove();
        return;
    }

    const section = document.createElement("div");
    section.id = "adminFlightsSection";
    section.style.cssText =
        "background:white; padding:25px; margin-top:25px; border-radius:12px;";

    section.innerHTML = `
        <h2 style="color:#14213d;">Manage Flights</h2>
        <p>All available airline flights</p>
<button id="addFlightBtn"
    style="background:#14213d; color:white; padding:12px 20px;
    border:0; border-radius:6px; cursor:pointer; margin:15px 0;">
    + Add Flight
</button>
        <div id="adminFlightsList">Loading flights...</div>
    `;

    dashboard.appendChild(section);
// Add Flight Button Function
document.getElementById("addFlightBtn").addEventListener("click", async function () {

    const flight_number = prompt("Enter Flight Number:");
    if (!flight_number) return;

    const origin = prompt("Enter Origin:");
    if (!origin) return;

    const destination = prompt("Enter Destination:");
    if (!destination) return;

    const departure_time = prompt("Enter Departure Time (YYYY-MM-DD HH:MM):");
    if (!departure_time) return;

    const price = Number(prompt("Enter Ticket Price:"));
    if (!Number.isFinite(price) || price <= 0) {
        alert("Invalid ticket price");
        return;
    }

    const seats_available = Number(prompt("Enter Available Seats:"));
    if (!Number.isInteger(seats_available) || seats_available < 0) {
        alert("Invalid number of seats");
        return;
    }

    try {
        const response = await fetch("/api/admin/flights", {
            method: "POST",
            headers: {
                "Content-Type": "application/json",
                "X-Admin-Token": adminToken
            },
            body: JSON.stringify({
                flight_number,
                origin,
                destination,
                departure_time,
                price,
                seats_available
            })
        });

        const data = await response.json();

        if (data.success) {
            alert("Flight added successfully!");
            const currentSection = document.getElementById("adminFlightsSection");
            if (currentSection) currentSection.remove();
            await window.showAdminFlights();
        } else {
            alert(data.message || "Could not add flight");
        }

    } catch (error) {
        alert("Server connection error");
        console.error(error);
    }
});
    try {
        const response = await fetch("/api/admin/flights", {
            headers: { "X-Admin-Token": adminToken }
        });
        const data = await response.json();

        if (!data.success || !data.flights) {
            document.getElementById("adminFlightsList").textContent =
                "Could not load flights.";
            return;
        }

        let html = `
            <table style="width:100%; border-collapse:collapse; margin-top:20px;">
                <tr style="background:#14213d; color:white;">
            <th style="padding:12px;">Flight</th>
<th>From</th>
<th>To</th>
<th>Departure</th>
<th>Price</th>
<th>Seats</th>
<th>Actions</th>
                </tr>
        `;

        data.flights.forEach(flight => {
            html += `
                <tr style="text-align:center; border-bottom:1px solid #ddd;">
                    <td style="padding:12px;">${flight.flight_number}</td>
                    <td>${flight.origin}</td>
                    <td>${flight.destination}</td>
                    <td>${flight.departure_time}</td>
                    <td>$${flight.price}</td>
                    <td>${flight.seats_available}</td>
<td>
    <button onclick="editFlight(${flight.id})">Edit</button>
    <button onclick="deleteFlight(${flight.id})">Delete</button>
</td>
                </tr>
            `;
        });

        html += `</table>`;

        document.getElementById("adminFlightsList").innerHTML = html;
    }
    catch (error) {
        document.getElementById("adminFlightsList").textContent =
            "Server connection error.";
        console.error(error);
    }
}



window.showAdminBookings = async function() {
    const old = document.getElementById("adminDataSection");
    if (old) old.remove();
    const section = document.createElement("section");
    section.id = "adminDataSection";
    section.style.cssText = "background:white;padding:25px;margin-top:25px;border-radius:12px;overflow:auto;";
    section.innerHTML = "<h2>Manage Bookings</h2><p>Loading bookings...</p>";
    dashboard.appendChild(section);
    try {
        const response = await fetch("/api/admin/bookings", {
            headers: { "X-Admin-Token": adminToken }
        });
        const data = await response.json();
        if (!data.success) throw new Error(data.message || "Could not load bookings");
        section.innerHTML = "<h2>Manage Bookings</h2>" +
          (data.bookings.length ? `<table style="width:100%;border-collapse:collapse"><tr><th>ID</th><th>Passenger</th><th>Passport</th><th>Flight</th><th>Status</th><th>Date</th></tr>` +
          data.bookings.map(b => `<tr><td>${b.id}</td><td>${b.passenger}</td><td>${b.passport}</td><td>${b.flight_number}</td><td>${b.status}</td><td>${b.booking_date}</td></tr>`).join("") + "</table>" : "<p>No bookings found.</p>");
    } catch (e) { section.innerHTML = "<h2>Manage Bookings</h2><p>" + e.message + "</p>"; }
};

window.showAdminPassengers = async function() {
    const old = document.getElementById("adminDataSection");
    if (old) old.remove();
    const section = document.createElement("section");
    section.id = "adminDataSection";
    section.style.cssText = "background:white;padding:25px;margin-top:25px;border-radius:12px;overflow:auto;";
    section.innerHTML = "<h2>Manage Passengers</h2><p>Loading passengers...</p>";
    dashboard.appendChild(section);
    try {
        const response = await fetch("/api/admin/passengers", {
            headers: { "X-Admin-Token": adminToken }
        });
        const data = await response.json();
        if (!data.success) throw new Error(data.message || "Could not load passengers");
        section.innerHTML = "<h2>Manage Passengers</h2>" +
          (data.passengers.length ? `<table style="width:100%;border-collapse:collapse"><tr><th>ID</th><th>Name</th><th>Phone</th><th>Passport</th></tr>` +
          data.passengers.map(p => `<tr><td>${p.id}</td><td>${p.name}</td><td>${p.phone}</td><td>${p.passport}</td></tr>`).join("") + "</table>" : "<p>No passengers found.</p>");
    } catch (e) { section.innerHTML = "<h2>Manage Passengers</h2><p>" + e.message + "</p>"; }
}

document.getElementById("adminLogoutButton").onclick = async function() {
    try {
        await fetch("/api/admin/logout", {
            method: "POST",
            headers: { "X-Admin-Token": adminToken }
        });
    } catch (error) {
        console.error("Logout request failed:", error);
    }
    adminToken = "";
    sessionStorage.removeItem("airlineos_admin_token");
    dashboard.remove();
};       
 } else {
            message.textContent = data.message || "Invalid username or password";
            message.style.color = "red";
        }
    } catch (error) {
        console.error(error);
        message.textContent = "Server connection error";
        message.style.color = "red";
    }
});

async function deleteFlight(flightId) {
    if (!confirm("Are you sure you want to delete this flight?")) {
        return;
    }

    try {
        const response = await fetch(`/api/admin/flights/${flightId}`, {
            method: "DELETE",
            headers: { "X-Admin-Token": adminToken }
        });

        const data = await response.json();

        if (data.success) {
            alert("Flight deleted successfully!");
            const currentSection = document.getElementById("adminFlightsSection");
            if (currentSection) currentSection.remove();
            window.showAdminFlights();
        } else {
            alert(data.message || "Could not delete flight.");
        }
    } catch (error) {
        console.error(error);
        alert("Server connection error.");
    }
}
async function editFlight(flightId) {
    const flightNumber = prompt("Enter new flight number:");
    if (flightNumber === null || flightNumber.trim() === "") return;

    const origin = prompt("Enter origin:");
    if (origin === null || origin.trim() === "") return;

    const destination = prompt("Enter destination:");
    if (destination === null || destination.trim() === "") return;

    const departureTime = prompt("Enter departure time (YYYY-MM-DD HH:MM):");
    if (departureTime === null || departureTime.trim() === "") return;

    const price = prompt("Enter price:");
    if (price === null || price.trim() === "") return;

    const seats = prompt("Enter available seats:");
    if (seats === null || seats.trim() === "") return;

    if (Number(price) <= 0 || Number(seats) < 0 ||
        !Number.isFinite(Number(price)) ||
        !Number.isInteger(Number(seats))) {
        alert("Please enter a valid price and seat number.");
        return;
    }

    try {
        const response = await fetch(`/api/admin/flights/${flightId}`, {
            method: "PUT",
            headers: {
                "Content-Type": "application/json",
                "X-Admin-Token": adminToken
            },
            body: JSON.stringify({
                flight_number: flightNumber.trim(),
                origin: origin.trim(),
                destination: destination.trim(),
                departure_time: departureTime.trim(),
                price: Number(price),
                seats_available: Number(seats)
            })
        });

        const data = await response.json();

        if (data.success) {
            alert("Flight updated successfully!");
            const currentSection = document.getElementById("adminFlightsSection");
            if (currentSection) currentSection.remove();
            window.showAdminFlights();
        } else {
            alert(data.message || "Could not update flight.");
        }
    } catch (error) {
        console.error(error);
        alert("Server connection error.");
    }
}
</script>

</body>
</html>
)HTML";

            crow::response response(200, html);
            response.set_header("Content-Type", "text/html; charset=utf-8");
            return response;
        });

    CROW_ROUTE(app, "/api/flights")
        ([db](const crow::request& req) {
        auto origin = req.url_params.get("origin");
        auto destination = req.url_params.get("destination");
        auto date = req.url_params.get("date");

        crow::json::wvalue result;

        if (!origin || !destination || !date) {
            result["error"] = "Missing search parameters";
            return result;
        }

        sqlite3_stmt* stmt = nullptr;

        const char* sql =
            "SELECT flight_number, origin, destination, departure_time, price, seats_available "
            "FROM Flights "
            "WHERE origin = ? AND destination = ? "
            "AND date(departure_time) = date(?) "
            "AND seats_available > 0";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["error"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_text(stmt, 1, origin, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, destination, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, date, -1, SQLITE_TRANSIENT);

        int index = 0;

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            result[index]["flight_number"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

            result[index]["origin"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

            result[index]["destination"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            result[index]["departure_time"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            result[index]["price"] = sqlite3_column_double(stmt, 4);

            result[index]["seats_available"] =
                sqlite3_column_int(stmt, 5);

            index++;
        }

        sqlite3_finalize(stmt);

        return result;
            });
    CROW_ROUTE(app, "/api/book").methods(crow::HTTPMethod::POST)
        ([db](const crow::request& req) {
        auto data = crow::json::load(req.body);

        crow::json::wvalue result;

        if (!data || !data.has("flight_number") ||
            !data.has("name") || !data.has("phone") ||
            !data.has("passport") || !data.has("seat_number")) {
            result["success"] = false;
            result["message"] = "Missing booking information";
            return result;
        }

        std::string flightNumber = data["flight_number"].s();
        std::string name = data["name"].s();
        std::string phone = data["phone"].s();
        std::string passport = data["passport"].s();
        std::string seatNumber = data["seat_number"].s();
        if (seatNumber.size() < 2 || seatNumber.size() > 4) {
            result["success"] = false;
            result["message"] = "Invalid seat number";
            return result;
        }

        sqlite3_stmt* stmt = nullptr;

        const char* passengerSQL =
            "INSERT INTO Passengers(name, phone, passport) "
            "VALUES(?, ?, ?) "
            "ON CONFLICT(passport) DO NOTHING;";

        if (sqlite3_prepare_v2(db, passengerSQL, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, phone.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, passport.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            sqlite3_finalize(stmt);
            return result;
        }

        sqlite3_finalize(stmt);

        stmt = nullptr;

        if (sqlite3_prepare_v2(db,
            "SELECT id FROM Passengers WHERE passport = ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_text(stmt, 1, passport.c_str(), -1, SQLITE_TRANSIENT);

        int passengerId = -1;

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            passengerId = sqlite3_column_int(stmt, 0);
        }

        sqlite3_finalize(stmt);

        if (passengerId < 0) {
            result["success"] = false;
            result["message"] = "Passenger not found";
            return result;
        }

        sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);

        stmt = nullptr;

        const char* bookingSQL =
            "INSERT INTO Bookings(passenger_id, flight_id, seat_number) "
            "SELECT ?, id, ? FROM Flights "
            "WHERE flight_number = ? AND seats_available > 0;";

        if (sqlite3_prepare_v2(db, bookingSQL, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_int(stmt, 1, passengerId);
        sqlite3_bind_text(stmt, 2, seatNumber.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, flightNumber.c_str(), -1, SQLITE_TRANSIENT);

        int bookingResult = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (bookingResult != SQLITE_DONE || sqlite3_changes(db) == 0) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = "Flight unavailable, no seats remain, or selected seat is already booked";
            return result;
        }

        stmt = nullptr;

        if (sqlite3_prepare_v2(db,
            "UPDATE Flights SET seats_available = seats_available - 1 "
            "WHERE flight_number = ?;",
            -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_text(stmt, 1, flightNumber.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            sqlite3_finalize(stmt);
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_finalize(stmt);
        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

        result["success"] = true;
        result["message"] = "Booking completed successfully";

        return result;
            });

    CROW_ROUTE(app, "/api/seats")
        ([db](const crow::request& req) {
        crow::json::wvalue result;
        crow::json::wvalue::list booked;
        auto flightNumber = req.url_params.get("flight_number");
        if (!flightNumber) {
            result["success"] = false; result["message"] = "Flight number is required"; return result;
        }
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT b.seat_number FROM Bookings b JOIN Flights f ON f.id=b.flight_id WHERE f.flight_number=? AND b.status='Confirmed' AND b.seat_number IS NOT NULL";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false; result["message"] = sqlite3_errmsg(db); return result;
        }
        sqlite3_bind_text(stmt, 1, flightNumber, -1, SQLITE_TRANSIENT);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char* value = sqlite3_column_text(stmt, 0);
            if (value) booked.emplace_back(reinterpret_cast<const char*>(value));
        }
        sqlite3_finalize(stmt);
        result["success"] = true; result["booked"] = std::move(booked); return result;
            });

    CROW_ROUTE(app, "/api/bookings")
        ([db](const crow::request& req) {
        crow::json::wvalue result;
        crow::json::wvalue::list bookings;

        auto passport = req.url_params.get("passport");

        if (!passport || std::string(passport).empty()) {
            result["success"] = false;
            result["message"] = "Passport number is required";
            return result;
        }

        sqlite3_stmt* stmt = nullptr;

        const char* sql =
            "SELECT b.id, f.flight_number, f.origin, f.destination, "
            "f.departure_time, f.price, b.booking_date, b.status, b.seat_number "
            "FROM Bookings b "
            "JOIN Passengers p ON b.passenger_id = p.id "
            "JOIN Flights f ON b.flight_id = f.id "
            "WHERE p.passport = ? "
            "ORDER BY b.id DESC;";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_text(stmt, 1, passport, -1, SQLITE_TRANSIENT);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue item;

            item["id"] = sqlite3_column_int(stmt, 0);
            item["flight_number"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            item["origin"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item["destination"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            item["departure_time"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            item["price"] = sqlite3_column_double(stmt, 5);
            item["booking_date"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
            item["status"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
            const unsigned char* seatText = sqlite3_column_text(stmt, 8);
            item["seat_number"] = seatText ? reinterpret_cast<const char*>(seatText) : "";

            bookings.push_back(std::move(item));
        }

        sqlite3_finalize(stmt);

        result["success"] = true;
        result["bookings"] = std::move(bookings);

        return result;
            });

    CROW_ROUTE(app, "/api/cancel").methods(crow::HTTPMethod::POST)
        ([db](const crow::request& req) {
        auto data = crow::json::load(req.body);
        crow::json::wvalue result;

        if (!data || !data.has("booking_id")) {
            result["success"] = false;
            result["message"] = "Booking ID is required";
            return result;
        }

        int bookingId = data["booking_id"].i();

        sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);

        sqlite3_stmt* stmt = nullptr;

        const char* checkSQL =
            "SELECT flight_id FROM Bookings "
            "WHERE id = ? AND status = 'Confirmed';";

        if (sqlite3_prepare_v2(db, checkSQL, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_int(stmt, 1, bookingId);

        int flightId = -1;

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            flightId = sqlite3_column_int(stmt, 0);
        }

        sqlite3_finalize(stmt);

        if (flightId < 0) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = "Booking not found or already cancelled";
            return result;
        }

        const char* cancelSQL =
            "UPDATE Bookings SET status = 'Cancelled' WHERE id = ?;";

        if (sqlite3_prepare_v2(db, cancelSQL, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_int(stmt, 1, bookingId);
        int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = "Could not cancel booking";
            return result;
        }

        const char* seatSQL =
            "UPDATE Flights SET seats_available = seats_available + 1 WHERE id = ?;";

        if (sqlite3_prepare_v2(db, seatSQL, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return result;
        }

        sqlite3_bind_int(stmt, 1, flightId);
        rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE || sqlite3_changes(db) == 0) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            result["success"] = false;
            result["message"] = "Could not restore flight seat";
            return result;
        }

        sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);

        result["success"] = true;
        result["message"] = "Booking cancelled successfully";
        return result;
            });


    // Admin Login API
    CROW_ROUTE(app, "/api/admin/login").methods(crow::HTTPMethod::POST)
        ([](const crow::request& req) {
        auto data = crow::json::load(req.body);
        crow::json::wvalue result;

        if (!data || !data.has("username") || !data.has("password")) {
            result["success"] = false;
            result["message"] = "Username and password are required";
            return crow::response(400, result);
        }

        std::string username = data["username"].s();
        std::string password = data["password"].s();

        if (username == "admin" && password == "admin123") {
            const std::string token = createAdminToken();
            {
                std::lock_guard<std::mutex> lock(g_adminMutex);
                g_adminToken = token;
            }
            result["success"] = true;
            result["message"] = "Admin login successful";
            result["token"] = token;
            return crow::response(200, result);
        }

        result["success"] = false;
        result["message"] = "Invalid username or password";
        return crow::response(401, result);
            });

    CROW_ROUTE(app, "/api/admin/logout").methods(crow::HTTPMethod::POST)
        ([](const crow::request& req) {
        crow::json::wvalue result;
        const std::string supplied = req.get_header_value("X-Admin-Token");
        {
            std::lock_guard<std::mutex> lock(g_adminMutex);
            if (!supplied.empty() && supplied == g_adminToken) g_adminToken.clear();
        }
        result["success"] = true;
        result["message"] = "Logged out";
        return crow::response(200, result);
            });

    // Admin: Get all flights
    CROW_ROUTE(app, "/api/admin/flights")
        .methods(crow::HTTPMethod::GET)
        ([&db](const crow::request& req) {
        if (!isAdminAuthorized(req)) {
            crow::json::wvalue denied; denied["success"] = false; denied["message"] = "Unauthorized";
            return crow::response(401, denied);
        }
        crow::json::wvalue result;
        crow::json::wvalue::list flights;

        sqlite3_stmt* stmt = nullptr;

        const char* sql =
            "SELECT id, flight_number, origin, destination, "
            "departure_time, price, seats_available "
            "FROM Flights ORDER BY id DESC";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return crow::response(500, result);
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue flight;

            flight["id"] = sqlite3_column_int(stmt, 0);

            flight["flight_number"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

            flight["origin"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

            flight["destination"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));

            flight["departure_time"] =
                reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));

            flight["price"] = sqlite3_column_double(stmt, 5);

            flight["seats_available"] = sqlite3_column_int(stmt, 6);

            flights.push_back(std::move(flight));
        }

        sqlite3_finalize(stmt);

        result["success"] = true;
        result["flights"] = std::move(flights);

        return crow::response(200, result);
            });

    // Admin: Add New Flight
    CROW_ROUTE(app, "/api/admin/flights")
        .methods(crow::HTTPMethod::POST)
        ([&db](const crow::request& req) {
        if (!isAdminAuthorized(req)) {
            crow::json::wvalue denied; denied["success"] = false; denied["message"] = "Unauthorized";
            return crow::response(401, denied);
        }
        crow::json::wvalue result;

        auto data = crow::json::load(req.body);

        // Check required fields
        if (!data ||
            !data.has("flight_number") ||
            !data.has("origin") ||
            !data.has("destination") ||
            !data.has("departure_time") ||
            !data.has("price") ||
            !data.has("seats_available")) {

            result["success"] = false;
            result["message"] = "All flight fields are required";

            return crow::response(400, result);
        }

        std::string flight_number = data["flight_number"].s();
        std::string origin = data["origin"].s();
        std::string destination = data["destination"].s();
        std::string departure_time = data["departure_time"].s();

        double price = data["price"].d();
        int seats_available = data["seats_available"].i();

        if (price <= 0 || seats_available < 0) {
            result["success"] = false;
            result["message"] = "Invalid price or seats number";

            return crow::response(400, result);
        }

        const char* sql =
            "INSERT INTO Flights "
            "(flight_number, origin, destination, departure_time, price, seats_available) "
            "VALUES (?, ?, ?, ?, ?, ?)";

        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {

            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);

            return crow::response(500, result);
        }

        sqlite3_bind_text(stmt, 1, flight_number.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, origin.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, destination.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, departure_time.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, price);
        sqlite3_bind_int(stmt, 6, seats_available);

        int rc = sqlite3_step(stmt);

        sqlite3_finalize(stmt);

        if (rc != SQLITE_DONE) {

            result["success"] = false;
            result["message"] = "Could not add flight. Flight number may already exist.";

            return crow::response(400, result);
        }

        result["success"] = true;
        result["message"] = "Flight added successfully";

        return crow::response(200, result);
            });
    // Admin: Update Flight
    CROW_ROUTE(app, "/api/admin/flights/<int>")
        .methods(crow::HTTPMethod::PUT)
        ([&db](const crow::request& req, int flightId) {
        if (!isAdminAuthorized(req)) {
            crow::json::wvalue denied; denied["success"] = false; denied["message"] = "Unauthorized";
            return crow::response(401, denied);
        }
        crow::json::wvalue result;

        auto data = crow::json::load(req.body);

        if (!data ||
            !data.has("flight_number") ||
            !data.has("origin") ||
            !data.has("destination") ||
            !data.has("departure_time") ||
            !data.has("price") ||
            !data.has("seats_available")) {

            result["success"] = false;
            result["message"] = "All flight fields are required";
            return crow::response(400, result);
        }

        std::string flightNumber = data["flight_number"].s();
        std::string origin = data["origin"].s();
        std::string destination = data["destination"].s();
        std::string departureTime = data["departure_time"].s();

        double price = data["price"].d();
        int seatsAvailable = data["seats_available"].i();

        if (price <= 0 || seatsAvailable < 0) {
            result["success"] = false;
            result["message"] = "Invalid price or seats number";
            return crow::response(400, result);
        }

        const char* sql =
            "UPDATE Flights SET "
            "flight_number = ?, "
            "origin = ?, "
            "destination = ?, "
            "departure_time = ?, "
            "price = ?, "
            "seats_available = ? "
            "WHERE id = ?;";

        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return crow::response(500, result);
        }

        sqlite3_bind_text(stmt, 1, flightNumber.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, origin.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, destination.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, departureTime.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(stmt, 5, price);
        sqlite3_bind_int(stmt, 6, seatsAvailable);
        sqlite3_bind_int(stmt, 7, flightId);

        int rc = sqlite3_step(stmt);
        int changes = sqlite3_changes(db);

        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE && changes > 0) {
            result["success"] = true;
            result["message"] = "Flight updated successfully";
            return crow::response(200, result);
        }

        result["success"] = false;
        result["message"] = "Flight not found or no changes made";
        return crow::response(400, result);
            });
    // Admin: list bookings
    CROW_ROUTE(app, "/api/admin/bookings")
        ([&db](const crow::request& req) {
        if (!isAdminAuthorized(req)) {
            crow::json::wvalue denied; denied["success"] = false; denied["message"] = "Unauthorized";
            return crow::response(401, denied);
        }
        crow::json::wvalue result;
        crow::json::wvalue::list bookings;
        sqlite3_stmt* stmt = nullptr;
        const char* sql =
            "SELECT b.id, p.name, p.passport, f.flight_number, b.status, b.booking_date, b.seat_number "
            "FROM Bookings b JOIN Passengers p ON p.id=b.passenger_id "
            "JOIN Flights f ON f.id=b.flight_id ORDER BY b.id DESC";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return crow::response(500, result);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue item;
            item["id"] = sqlite3_column_int(stmt, 0);
            item["passenger"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            item["passport"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item["flight_number"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            item["status"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
            item["booking_date"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
            const unsigned char* seatValue = sqlite3_column_text(stmt, 6);
            item["seat_number"] = seatValue ? reinterpret_cast<const char*>(seatValue) : "";
            bookings.push_back(std::move(item));
        }
        sqlite3_finalize(stmt);
        result["success"] = true;
        result["bookings"] = std::move(bookings);
        return crow::response(200, result);
            });

    // Admin: list passengers
    CROW_ROUTE(app, "/api/admin/passengers")
        ([&db](const crow::request& req) {
        if (!isAdminAuthorized(req)) {
            crow::json::wvalue denied; denied["success"] = false; denied["message"] = "Unauthorized";
            return crow::response(401, denied);
        }
        crow::json::wvalue result;
        crow::json::wvalue::list passengers;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT id, name, phone, passport FROM Passengers ORDER BY id DESC";
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return crow::response(500, result);
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            crow::json::wvalue item;
            item["id"] = sqlite3_column_int(stmt, 0);
            item["name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            item["phone"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
            item["passport"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
            passengers.push_back(std::move(item));
        }
        sqlite3_finalize(stmt);
        result["success"] = true;
        result["passengers"] = std::move(passengers);
        return crow::response(200, result);
            });

    // Admin Dashboard Statistics API
    CROW_ROUTE(app, "/api/admin/stats")
        ([&db](const crow::request& req) {
        if (!isAdminAuthorized(req)) {
            crow::json::wvalue denied; denied["success"] = false; denied["message"] = "Unauthorized";
            return crow::response(401, denied);
        }
        crow::json::wvalue result;

        const char* sql =
            "SELECT "
            "(SELECT COUNT(*) FROM Passengers), "
            "(SELECT COUNT(*) FROM Flights), "
            "(SELECT COUNT(*) FROM Bookings WHERE status = 'Confirmed'), "
            "(SELECT COUNT(*) FROM Bookings WHERE status = 'Cancelled'), "
            "(SELECT COALESCE(SUM(f.price), 0) "
            "FROM Bookings b JOIN Flights f ON b.flight_id = f.id "
            "WHERE b.status = 'Confirmed');";

        sqlite3_stmt* stmt = nullptr;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = sqlite3_errmsg(db);
            return crow::response(500, result);
        }

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result["success"] = true;
            result["total_passengers"] = sqlite3_column_int(stmt, 0);
            result["total_flights"] = sqlite3_column_int(stmt, 1);
            result["confirmed_bookings"] = sqlite3_column_int(stmt, 2);
            result["cancelled_bookings"] = sqlite3_column_int(stmt, 3);
            result["total_revenue"] = sqlite3_column_double(stmt, 4);
        }
        else {
            result["success"] = false;
            result["message"] = "Could not load statistics";
        }

        sqlite3_finalize(stmt);
        return crow::response(200, result);
            });
    // Admin: Delete Flight
    CROW_ROUTE(app, "/api/admin/flights/<int>")
        .methods(crow::HTTPMethod::Delete)
        ([&db](const crow::request& req, int flightId) {
        if (!isAdminAuthorized(req)) {
            crow::json::wvalue denied; denied["success"] = false; denied["message"] = "Unauthorized";
            return crow::response(401, denied);
        }
        crow::json::wvalue result;

        sqlite3_stmt* stmt = nullptr;

        const char* sql =
            "DELETE FROM Flights "
            "WHERE id = ? "
            "AND NOT EXISTS ("
            "SELECT 1 FROM Bookings WHERE flight_id = ?"
            ")";

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            result["success"] = false;
            result["message"] = "Database error";
            return crow::response(500, result);
        }

        sqlite3_bind_int(stmt, 1, flightId);
        sqlite3_bind_int(stmt, 2, flightId);

        int rc = sqlite3_step(stmt);
        int changes = sqlite3_changes(db);

        sqlite3_finalize(stmt);

        if (rc == SQLITE_DONE && changes > 0) {
            result["success"] = true;
            result["message"] = "Flight deleted successfully";
            return crow::response(200, result);
        }

        result["success"] = false;
        result["message"] = "Flight not found or has bookings";
        return crow::response(400, result);
            });
    const char* portEnv = std::getenv("PORT");
const int port = portEnv ? std::stoi(portEnv) : 8080;
app.port(port).bindaddr("0.0.0.0").run();

    if (db) sqlite3_close(db);
    return 0;
}
