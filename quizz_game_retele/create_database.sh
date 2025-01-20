#!/bin/bash
if ! command -v sqlite3 &> /dev/null; then
    echo "Error: sqlite3 nu este instalat. Instalati-l folosind:"
    echo "sudo apt-get install sqlite3"
    exit 1
fi
rm -f questions.db
sqlite3 questions.db < create_db.sql
if [ -f "questions.db" ]; then
    echo "Baza de date a fost creata cu succes!"
    echo "Verificare continut:"
    sqlite3 questions.db "SELECT COUNT(*) as 'Numar total de intrebari' FROM intrebari;"
    echo "Numar de intrebari per dificultate:"
    sqlite3 questions.db "SELECT dificultate, COUNT(*) as 'count' FROM intrebari GROUP BY dificultate;"
else
    echo "A aparut o eroare la crearea bazei de date!"
fi
