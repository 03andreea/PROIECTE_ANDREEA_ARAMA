use rusqlite::{params,Connection,Result};
use sha2::{Digest,Sha256};
use std::path::Path;
use std::io::{self,Write,Read};
use std::fs;
use std::time::{SystemTime,UNIX_EPOCH};
use std::collections::HashMap;
use std::collections::HashSet;
fn init_repository()->Result<(),Box<dyn std::error::Error>>
{
    let db_path = Path::new(".vcs").join("repo.db");
    if db_path.exists()
    {
        return Err(Box::new(io::Error::new(io::ErrorKind::AlreadyExists,"Repository deja initializat.",)));
    }
    std::fs::create_dir_all(".vcs")?;
    if !Path::new(".gitignore").exists()
    {
        fs::write(".gitignore", "*.tmp\n.vcs/\nsecret.txt\n*.log\nceva.txt")?;
    }
    let conn = Connection::open(db_path)?;
    conn.execute(
        "CREATE TABLE branches (id INTEGER PRIMARY KEY, branch_name TEXT UNIQUE NOT NULL,
        current_commit TEXT DEFAULT NULL);",[],)?;
    conn.execute(
        "CREATE TABLE commits (id INTEGER PRIMARY KEY, 
        commit_hash TEXT UNIQUE NOT NULL, message TEXT NOT NULL, parent_commit TEXT, timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);",
        [],
    )?;
    conn.execute(
    "CREATE TABLE tracked_files (
        id INTEGER PRIMARY KEY,
        branch_name TEXT NOT NULL,
        file_name TEXT NOT NULL,
        commit_hash TEXT NOT NULL,
        file_hash TEXT NOT NULL,
        file_content TEXT NOT NULL,
        FOREIGN KEY(branch_name) REFERENCES branches(branch_name)
    );",
    [],
)?;
    conn.execute(
        "CREATE TABLE staging_area(
        file_name TEXT NOT NULL, branch_name TEXT, status NOT NULL CHECK (status in ('new file','modified','deleted')),timestamp DATETIME NOT NULL,
        file_hash TEXT NOT NULL, UNIQUE(file_name, branch_name))",[]
    )?;
    conn.execute("CREATE TABLE IF NOT EXISTS current_branch (branch_name TEXT PRIMARY KEY)",[],)?;
    conn.execute("INSERT OR IGNORE INTO current_branch (branch_name) values ('main')",[],)?;
    conn.execute("INSERT INTO branches (branch_name,current_commit) values (?1,?2);",params!["main",""])?;
    println!("Repository initializat cu succes!");
    Ok(())
}
fn calculate_file_hash(file_path: &str)-> Result<String,Box<dyn std::error::Error>>
{
    let mut file = fs::File::open(file_path)?;
    let mut content = Vec::new();
    file.read_to_end(&mut content)?;
    let mut hashing = Sha256::new();
    hashing.update(&content);
    let hash = hashing.finalize();
    Ok(format!("{:x}",hash))
}
fn ignored(file_name: &str) -> bool 
{
    if file_name == ".gitignore"
    {
        return true;
    }
    if let Ok(ignore_content) = fs::read_to_string(".gitignore") 
    {
        let patterns: Vec<&str> = ignore_content.lines().collect();
        for pattern in patterns 
        {
            if pattern.trim().is_empty() || pattern.starts_with('#') 
            {
                continue;
            }
            let pattern = pattern.trim();
            if pattern.ends_with('/') 
            {
                if file_name.starts_with(pattern) 
                {
                    return true;
                }
            } else if pattern.contains('*'){
                let pattern = pattern.replace("*", ".*");
                if regex::Regex::new(&pattern).unwrap().is_match(file_name) 
                {
                    return true;
                }
            } else if file_name == pattern {
                return true;
            }
        }
    }
    false
}
fn get_current_branch() -> Result<String,rusqlite::Error>
{
    let conn = Connection::open(".vcs/repo.db")?;
    let mut stmt = conn.prepare("SELECT branch_name FROM current_branch limit 1")?;
    let branch:String = stmt.query_row([],|row| row.get(0))?;
    Ok(branch)
}
fn git_add(file_name: &str) -> Result<(), Box<dyn std::error::Error>> 
{
    if ignored(file_name) 
    {
        println!("Ignoram fisierele '{}' (care se afla in .gitignore)", file_name);
        return Ok(());
    }
    let conn = Connection::open(".vcs/repo.db")?;
    let branch = get_current_branch()?;
    let mut stmt = conn.prepare(
        "SELECT EXISTS (SELECT 1 FROM tracked_files WHERE branch_name = ? AND file_name = ?
        )"
    )?;
    
    let was_ever_tracked: bool = stmt.query_row(params![&branch, file_name], |row| row.get(0))?;
    let file_exists = std::path::Path::new(file_name).exists();
    if !was_ever_tracked && !file_exists 
    {
        return Err(Box::new(io::Error::new(io::ErrorKind::NotFound,format!("Fisierul '{}' nu exista!",file_name),)));
    }
    let (status,file_hash) = if !file_exists && was_ever_tracked {
        ("deleted","deleted".to_string())
    } else if was_ever_tracked {
        ("modified",calculate_file_hash(file_name)?)
    } else {
        ("new file",calculate_file_hash(file_name)?)
    };
    let timestamp = SystemTime::now().duration_since(UNIX_EPOCH)?.as_secs();

    conn.execute(
        "INSERT INTO staging_area (file_name, branch_name, status, timestamp, file_hash) 
         VALUES (?1, ?2, ?3, ?4, ?5) 
         ON CONFLICT (file_name, branch_name) 
         DO UPDATE SET status = excluded.status, timestamp = excluded.timestamp, file_hash = excluded.file_hash",
        params![file_name, branch, status, timestamp, file_hash],
    )?;

    println!(
        "Fisierul '{}' a fost adaugat in staging area pe branch-ul '{}'.",
        file_name, branch
    );
    
    Ok(())
}
fn show_staging_area()->Result<(),rusqlite::Error>
{
    let conn = Connection::open(".vcs/repo.db")?;
    let branch = get_current_branch()?;
    let mut stmt = conn.prepare("SELECT file_name,status FROM staging_area WHERE branch_name = ?1")?;
    let rows = stmt.query_map([&branch], |row| {Ok((row.get::<_,String>(0)?,row.get::<_,String>(1)?))})?;
    println!("Staging area pentru branch-ul '{}':",branch);
    for row in rows
    {
        let (file_name,status) = row?;
        println!(" {} ({})", file_name, status);
    }
    Ok(())
}
fn checkout_branch(branch_name: &str) -> Result<(), Box<dyn std::error::Error>> 
{
    let conn = Connection::open(".vcs/repo.db")?;
    let current_branch = get_current_branch()?;
    
    let mut stmt = conn.prepare(
        "SELECT EXISTS(SELECT 1 FROM branches WHERE branch_name = ?)"
    )?;
    let branch_exists: bool = stmt.query_row([branch_name], |row| row.get(0))?;
    
    if !branch_exists 
    {
        let mut stmt = conn.prepare(
            "SELECT current_commit FROM branches WHERE branch_name = ?"
        )?;
        let current_commit: String = stmt.query_row([&current_branch], |row| row.get(0))?;
        conn.execute(
            "INSERT INTO branches (branch_name, current_commit) VALUES (?1, ?2)",
            params![branch_name, &current_commit],
        )?;
        let mut stmt = conn.prepare(
            "WITH latest_files AS (
                SELECT file_name, MAX(commit_hash) as last_commit
                FROM tracked_files
                WHERE branch_name = ?
                GROUP BY file_name
            )
            SELECT tf.file_name, tf.commit_hash, tf.file_hash, tf.file_content
            FROM tracked_files tf
            JOIN latest_files lf ON tf.file_name = lf.file_name 
            AND tf.commit_hash = lf.last_commit
            WHERE tf.branch_name = ?"
        )?;
 
        let files: Vec<(String, String, String, String)> = stmt
            .query_map(params![&current_branch, &current_branch], |row| {
                Ok((
                    row.get(0)?, 
                    row.get(1)?, 
                    row.get(2)?, 
                    row.get(3)?, 
                ))
            })?
            .collect::<Result<Vec<_>, _>>()?;
 
        for (file_name, commit_hash, file_hash, content) in files 
        {
            conn.execute(
                "INSERT INTO tracked_files (branch_name, file_name, commit_hash, file_hash, file_content) 
                 VALUES (?1, ?2, ?3, ?4, ?5)",
                params![branch_name, file_name, commit_hash, file_hash, content],
            )?;
        }
    }
    conn.execute(
        "UPDATE current_branch SET branch_name = ?1",
        [branch_name],
    )?;
    let mut stmt = conn.prepare(
        "WITH latest_files AS (
            SELECT file_name, MAX(commit_hash) as last_commit
            FROM tracked_files
            WHERE branch_name = ?
            GROUP BY file_name
        )
        SELECT tf.file_name, tf.file_hash,tf.file_content
        FROM tracked_files tf
        JOIN latest_files lf ON tf.file_name = lf.file_name 
        AND tf.commit_hash = lf.last_commit
        WHERE tf.branch_name = ?"
    )?;
 
    let files: Vec<(String, String,String)> = stmt
        .query_map(params![branch_name, branch_name], |row| {
            Ok((
                row.get(0)?,
                row.get(1)?, 
                row.get(2)?,
            ))
        })?
        .collect::<Result<Vec<_>, _>>()?;

    for (file_name, file_hash, content) in files {
        if file_hash != "deleted" {
            fs::write(&file_name, content)?;
        } else if Path::new(&file_name).exists()
        {
            fs::remove_file(&file_name)?;
        }
    }
    println!("Switched to branch '{}'", branch_name);
    Ok(())
}
 
fn git_status() -> Result<(), Box<dyn std::error::Error>> 
{
    let conn = Connection::open(".vcs/repo.db")?;
    let branch = get_current_branch()?;
    let mut stmt = conn.prepare(
        "WITH latest_files AS (
            SELECT file_name, MAX(commit_hash) as last_commit
            FROM tracked_files
            WHERE branch_name = ?
            GROUP BY file_name
        )
        SELECT tf.file_name, tf.file_hash
        FROM tracked_files tf
        JOIN latest_files lf ON tf.file_name = lf.file_name 
        AND tf.commit_hash = lf.last_commit
        WHERE tf.branch_name = ? AND tf.file_hash != 'deleted'"
    )?;

    let tracked_files: HashMap<String, String> = stmt
        .query_map(params![&branch, &branch], |row| {
            Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?))
        })?
        .collect::<Result<HashMap<_, _>, _>>()?;

    let mut stmt = conn.prepare(
        "SELECT file_name, status, file_hash 
         FROM staging_area 
         WHERE branch_name = ?"
    )?;
    
    let staged_files: Vec<(String, String, String)> = stmt
        .query_map([&branch], |row| {
            Ok((row.get(0)?, row.get(1)?, row.get(2)?))
        })?
        .collect::<Result<Vec<_>, _>>()?;

    let mut modified = Vec::new();
    let mut untracked = Vec::new();
    let mut deleted = Vec::new();
    for entry in fs::read_dir(".")? {
        let entry = entry?;
        let file_name = entry.file_name().to_string_lossy().to_string();
        
        if ignored(&file_name) || !entry.path().is_file() 
        {
            continue;
        }

        let current_hash = calculate_file_hash(&file_name)?;
        
        if !tracked_files.contains_key(&file_name) 
        {
            if !staged_files.iter().any(|(name, _, _)| name == &file_name) 
            {
                untracked.push(file_name);
            }
            continue;
        }

        if let Some(old_hash) = tracked_files.get(&file_name) 
        {
            if &current_hash != old_hash && !staged_files.iter().any(|(name, _, _)| name == &file_name) 
            {
                modified.push(file_name);
            }
        }
    }

    for file_name in tracked_files.keys() 
    {
        if !Path::new(file_name).exists() && !staged_files.iter().any(|(name, _, _)| name == file_name) 
        {
            deleted.push(file_name.clone());
        }
    }

    if !staged_files.is_empty() 
    {
        println!("Changes to be committed:");
        for (file, status, _) in &staged_files 
        {
            println!("  {}: {}", status, file);
        }
    }

    if !modified.is_empty() || !deleted.is_empty() 
    {
        println!("Changes not staged for commit:");
        for file in &modified 
        {
            println!("  modified: {}", file);
        }
        for file in &deleted 
        {
            println!("  deleted: {}", file);
        }
    }

    if !untracked.is_empty() 
    {
        println!("Untracked files:");
        for file in &untracked 
        {
            println!("  {}", file);
        }
    }

    if staged_files.is_empty() && modified.is_empty() && deleted.is_empty() && untracked.is_empty() 
    {
        println!("Working tree clean");
    }

    Ok(())
}
fn generate_commit_hash() -> String
{
    let now = SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_secs();
    format!("{:x}",now)
}
fn git_commit(message: &str) -> Result<(), Box<dyn std::error::Error>> 
{
    let conn = Connection::open(".vcs/repo.db")?;
    let branch = get_current_branch()?;

    let mut stmt = conn.prepare("SELECT COALESCE(current_commit, '') FROM branches WHERE branch_name = ?")?;
    let parent_commit: String = stmt.query_row([&branch], |row| row.get(0))?;

    let commit_hash = generate_commit_hash();

    conn.execute(
        "INSERT INTO commits (commit_hash, message, parent_commit) VALUES (?1, ?2, ?3)",
        params![commit_hash, message, parent_commit],
    )?;
    let mut stmt = conn.prepare(
        "SELECT file_name, file_hash FROM staging_area WHERE branch_name = ?"
    )?;

    let staged_files: Vec<(String, String)> = stmt
        .query_map([&branch], |row| Ok((row.get(0)?, row.get(1)?)))?
        .collect::<Result<Vec<_>, _>>()?;
    for (file_name, file_hash) in staged_files 
    {
        if file_hash == "deleted"
        {
            conn.execute("INSERT INTO tracked_files (branch_name, file_name, commit_hash, file_hash, file_content) VALUES (?1,?2,?3,?4,?5)",params![&branch,file_name,commit_hash,"deleted",""],)?;
        }
        else 
        {
            let content = fs::read_to_string(&file_name)?;
            conn.execute(
                "INSERT INTO tracked_files (branch_name, file_name, commit_hash, file_hash, file_content) 
                VALUES (?1, ?2, ?3, ?4, ?5)",
                params![&branch, file_name, commit_hash, file_hash, content],
            )?;
        }
    }
    conn.execute(
        "UPDATE branches SET current_commit = ?1 WHERE branch_name = ?2",
        params![commit_hash, &branch],
    )?;
    conn.execute(
        "DELETE FROM staging_area WHERE branch_name = ?1",
        params![&branch],
    )?;

    println!("Commit-ul '{}' a fost realizat cu succes pe branch-ul '{}'.", commit_hash, branch);

    Ok(())
}
fn git_diff_branches(branch1: &str, branch2: &str) -> Result<(), Box<dyn std::error::Error>> {
    let conn = Connection::open(".vcs/repo.db")?;
    
    let mut stmt = conn.prepare(
        "WITH latest_commits AS (
            SELECT branch_name, current_commit
            FROM branches
            WHERE branch_name IN (?, ?)
        )
        SELECT tf.branch_name, tf.file_name, tf.file_hash, tf.file_content
        FROM tracked_files tf
        JOIN latest_commits lc ON tf.branch_name = lc.branch_name
        WHERE tf.commit_hash = lc.current_commit"
    )?;
 
    let mut branch1_files: HashMap<String, (String, String)> = HashMap::new();
    let mut branch2_files: HashMap<String, (String, String)> = HashMap::new();
 
    let rows = stmt.query_map(params![branch1, branch2], |row| {
        Ok((
            row.get::<_, String>(0)?,
            row.get::<_, String>(1)?,
            row.get::<_, String>(2)?,
            row.get::<_, String>(3)?,
        ))
    })?;
 
    for result in rows 
    {
        let (branch_name, file_name, hash, content) = result?;
        if branch_name == branch1 
        {
            branch1_files.insert(file_name, (hash, content));
        } 
        else 
        {
            branch2_files.insert(file_name, (hash, content));
        }
    }
 
    println!("\nComparam branch-urile '{}' si '{}':", branch1, branch2);
    println!("----------------------------------------");

    let specific_to_branch1: HashSet<_> = branch1_files.keys()
        .filter(|k| !branch2_files.contains_key(*k) || 
               branch1_files[*k].0 != branch2_files[*k].0)
        .collect();
 
    if !specific_to_branch1.is_empty() 
    {
        println!("\nFisiere specifice {}:", branch1);
        for file_name in &specific_to_branch1 
        {
            println!("- {}:", file_name);
            for line in branch1_files[*file_name].1.lines() 
            {
                println!("  {}", line);
            }
        }
    }
    let specific_to_branch2: HashSet<_> = branch2_files.keys()
        .filter(|k| !branch1_files.contains_key(*k) || 
               branch2_files[*k].0 != branch1_files[*k].0)
        .collect();
 
    if !specific_to_branch2.is_empty() 
    {
        println!("\nFisiere specifice pe {}:", branch2);
        for file_name in &specific_to_branch2 
        {
            println!("- {}:", file_name);
            for line in branch2_files[*file_name].1.lines() 
            {
                println!("  {}", line);
            }
        }
    }
    let common_modified_files: HashSet<_> = branch1_files.keys()
        .filter(|k| branch2_files.contains_key(*k) && 
               branch1_files[*k].0 != branch2_files[*k].0)
        .collect();
 
    if !common_modified_files.is_empty() 
    {
        println!("\nDiferente intre fisierele comune:");
        for file_name in common_modified_files {
            println!("\nModificat: {}", file_name);
            println!("Schimbari intre {} si {}:", branch1, branch2);
            let old_lines: Vec<&str> = branch1_files[file_name].1.lines().collect();
            let new_lines: Vec<&str> = branch2_files[file_name].1.lines().collect();
            
            let mut line_num = 1;
            let min_len = std::cmp::min(old_lines.len(), new_lines.len());
 
            for i in 0..min_len 
            {
                if old_lines[i] != new_lines[i] 
                {
                    println!("Linie {}:", line_num);
                    println!("-- {}", old_lines[i]);
                    println!("++ {}", new_lines[i]);
                }
                line_num += 1;
            }
 
            match old_lines.len().cmp(&new_lines.len()) 
            {
                std::cmp::Ordering::Less => {
                    for line in new_lines.iter().skip(min_len) 
                    {
                        println!("++ {}", line);
                    }
                }
                std::cmp::Ordering::Greater => {
                    for line in old_lines.iter().skip(min_len) 
                    {
                        println!("-- {}", line);
                    }
                }
                _ => (), 
            }
        }
    }
    Ok(())
}
fn git_diff_with_previous() -> Result<(), Box<dyn std::error::Error>> 
{
    let conn = Connection::open(".vcs/repo.db")?;
    let branch = get_current_branch()?;
    
    let mut stmt = conn.prepare(
        "SELECT current_commit FROM branches WHERE branch_name = ?"
    )?;
    let current_commit: String = stmt.query_row([&branch], |row| row.get(0))?;
    
    if current_commit.is_empty() 
    {
        println!("Nu s-au facut commit-uri inca.");
        return Ok(());
    }

    let mut stmt = conn.prepare(
        "SELECT parent_commit FROM commits WHERE commit_hash = ?"
    )?;
    
    let parent_commit: Option<String> = stmt.query_row([&current_commit], |row| row.get(0))?;

    if let Some(parent) = parent_commit 
    {
        println!("Comparam commit-urile {} si {}:", parent, current_commit);
        println!("----------------------------------------");
        let mut stmt = conn.prepare(
            "SELECT DISTINCT tf.file_name, tf.file_hash, tf.file_content
             FROM tracked_files tf
             WHERE tf.commit_hash = ? AND tf.branch_name = ? 
             AND tf.file_hash != 'deleted'"
        )?;

        let current_files: HashMap<String, (String, String)> = stmt
            .query_map(params![&current_commit, &branch], |row| {
                Ok((
                    row.get::<_, String>(0)?,
                    (row.get::<_, String>(1)?, row.get::<_, String>(2)?)
                ))
            })?
            .collect::<Result<HashMap<_, _>, _>>()?;
        let mut stmt = conn.prepare(
            "SELECT DISTINCT tf.file_name, tf.file_hash, tf.file_content
             FROM tracked_files tf
             WHERE tf.commit_hash = ? AND tf.branch_name = ?
             AND tf.file_hash != 'deleted'"
        )?;

        let parent_files: HashMap<String, (String, String)> = stmt
            .query_map(params![&parent, &branch], |row| {
                Ok((
                    row.get::<_, String>(0)?,
                    (row.get::<_, String>(1)?, row.get::<_, String>(2)?)
                ))
            })?
            .collect::<Result<HashMap<_, _>, _>>()?;

        let mut stmt = conn.prepare(
            "SELECT DISTINCT file_name
             FROM tracked_files 
             WHERE commit_hash = ? AND branch_name = ?
             AND file_hash != 'deleted'"
        )?;

        let modified_files: HashSet<String> = stmt
            .query_map(params![&current_commit, &branch], |row| {
                row.get::<_, String>(0)
            })?
            .collect::<Result<HashSet<_>, _>>()?;

        let mut has_changes = false;

        for file_name in &modified_files 
        {
            let current_version = current_files.get(file_name);
            let parent_version = parent_files.get(file_name);

            if let (Some((old_hash, old_content)), Some((new_hash, new_content))) = (parent_version, current_version) {
                if old_hash != new_hash 
                {
                    has_changes = true;
                    println!("\nModificat: {}", file_name);
                    println!("Schimbari:");
                    
                    let old_lines: Vec<&str> = old_content.lines().collect();
                    let new_lines: Vec<&str> = new_content.lines().collect();
                    
                    let mut line_num = 1;
                    let min_len = std::cmp::min(old_lines.len(), new_lines.len());

                    for i in 0..min_len 
                    {
                        if old_lines[i] != new_lines[i] 
                        {
                            println!("Linie {}:", line_num);
                            println!("-- {}", old_lines[i]);
                            println!("++ {}", new_lines[i]);
                        }
                        line_num += 1;
                    }

                    for (i, _line) in old_lines.iter().enumerate().skip(min_len) 
                    {
                        println!("Linie {}:", i + 1);
                        println!("-- {}", old_lines[i]);
                    }
                    for (i, _line) in new_lines.iter().enumerate().skip(min_len) 
                    {
                        println!("Linie {}:", i + 1);
                        println!("++ {}", new_lines[i]);
                    }
                }
            } else if current_version.is_some() && parent_version.is_none() 
            {
                has_changes = true;
                let (_, current_content) = current_files.get(file_name).unwrap();
                println!("\nFisier nou adaugat: {}", file_name);
                println!("Continut:");
                for line in current_content.lines() 
                {
                    println!("++ {}", line);
                }
            }
        }

        if !has_changes 
        {
            println!("Nu exista modificari.");
        }
    } 
    else 
    {
        println!("Nu exista commit anterior cu care sa putem compara.");
    }
    
    Ok(())
}
fn git_merge(source_branch: &str) -> Result<(), Box<dyn std::error::Error>> 
{
    let conn = Connection::open(".vcs/repo.db")?;
    let target_branch = get_current_branch()?;

    if source_branch == target_branch {
        return Err(Box::new(io::Error::new(
            io::ErrorKind::InvalidInput,
            "Nu putem face merge la un branch cu el insusi.",
        )));
    }

    let mut stmt = conn.prepare("SELECT current_commit FROM branches WHERE branch_name = ?")?;
    let source_commit: String = stmt.query_row([source_branch], |row| row.get(0))?;
    let target_commit: String = stmt.query_row([&target_branch], |row| row.get(0))?;

    let mut stmt = conn.prepare(
        "SELECT file_name, file_hash, file_content 
         FROM tracked_files 
         WHERE commit_hash = ?"
    )?;

    let source_files: HashMap<String, (String, String)> = stmt
        .query_map([&source_commit], |row| {
            Ok((
                row.get::<_, String>(0)?,
                (row.get::<_, String>(1)?, row.get::<_, String>(2)?),
            ))
        })?
        .collect::<Result<HashMap<_, _>, _>>()?;

    let target_files: HashMap<String, (String, String)> = stmt
        .query_map([&target_commit], |row| {
            Ok((
                row.get::<_, String>(0)?,
                (row.get::<_, String>(1)?, row.get::<_, String>(2)?),
            ))
        })?
        .collect::<Result<HashMap<_, _>, _>>()?;

    let merge_hash = generate_commit_hash();
    let message = format!("merge branch '{}' into '{}'", source_branch, target_branch);

    conn.execute(
        "INSERT INTO commits (commit_hash, message, parent_commit) VALUES (?1, ?2, ?3)",
        params![merge_hash, message, target_commit],
    )?;
    let mut merged_files = HashMap::new();
    let all_files: HashSet<_> = source_files.keys()
        .chain(target_files.keys())
        .collect();

    for file_name in all_files 
    {
        match (source_files.get(file_name), target_files.get(file_name)) 
        {
            (Some((hash_s, content_s)), Some((hash_t, content_t))) => {
                println!("File: {}", file_name);
                println!("Dimensiunea sursei: {}", content_s.len());
                println!("Continut sursa:\n{}", content_s);
                println!("Dimensiune target: {}", content_t.len());
                println!("Continut target:\n{}", content_t);
                
                if content_t.len() > content_s.len() 
                {
                    println!("Alegem fisierul target!");
                    merged_files.insert(file_name.clone(), (hash_t.clone(), content_t.clone()));
                } 
                else 
                {
                    println!("Alegem fisierul sursa!");
                    merged_files.insert(file_name.clone(), (hash_s.clone(), content_s.clone()));
                }
            },
            (Some((hash, content)), None) => {
                merged_files.insert(file_name.clone(), (hash.clone(), content.clone()));
            },
            (None, Some((hash, content))) => {
                merged_files.insert(file_name.clone(), (hash.clone(), content.clone()));
            },
            _ => unreachable!(),
        }
    }
    for (file_name, (hash, content)) in merged_files 
    {
        conn.execute(
            "INSERT INTO tracked_files (branch_name, file_name, commit_hash, file_hash, file_content) 
             VALUES (?1, ?2, ?3, ?4, ?5)",
            params![target_branch, file_name, merge_hash, hash, content],
        )?;
        fs::write(&file_name, content)?;
    }

    conn.execute(
        "UPDATE branches SET current_commit = ?1 WHERE branch_name = ?2",
        params![merge_hash, target_branch],
    )?;

    println!("Merge-ul lui '{}' in '{}' a fost realizat cu succes.", source_branch, target_branch);
    Ok(())
}
fn main() 
{
    println!("Bine ai venit in aplicatia my_svn!");
    let mut input = String::new();
    loop 
    {
        print!("svn> ");
        io::stdout().flush().unwrap();
        input.clear();
        if io::stdin().read_line(&mut input).is_err()
        {
            eprintln!("Eroare la citirea comenzii.");
            continue;
        }
        let comanda = input.trim();
        match comanda{
            "git init" =>
            {
                if let Err(e) = init_repository()
                {
                    eprintln!("Eroare: {}",e);
                }
            }
            "git branch --show-current"=>
            {
                match get_current_branch()
                {
                    Ok(branch) => { println!("Te afli pe branch-ul curent: {}",branch)},
                    Err(e)=> { eprintln!("Eroare la afisarea branch-ului curent: {}",e);},
                }
            }
            comanda if comanda.starts_with("git add ") =>
            {
                let file_name = comanda.strip_prefix("git add ").unwrap_or_default().trim();
                if file_name.is_empty()
                {
                    println!("Te rog sa specifici fisierul la care vrei sa dai add.");
                    continue;
                }
                if let Err(e) = git_add(file_name)
                {
                    eprintln!("Eroare: {}",e);
                }
            }
            "git status" =>
            {
                if let Err(e) = git_status()
                {
                    eprintln!("Eroare la git status: {}",e);
                }
            }
            comanda if comanda.starts_with("git commit -m ")=>
            {
                let message = comanda.strip_prefix("git commit -m ").unwrap_or_default().trim();
                if message.is_empty()
                {
                    println!("Te rog sa furnizezi un mesaj pentru commit.");
                    continue;
                }
                if let Err(e) = git_commit(message)
                {
                    eprintln!("Eroare la realizarea commit-ului: {}",e);
                }
            }
            comanda if comanda.starts_with("git checkout ") =>
            {
                let branch_name = comanda.strip_prefix("git checkout ").unwrap_or_default().trim();
                if let Err(e) = checkout_branch(branch_name)
                {
                    eprintln!("Eroare la schimbarea branch-ului: {}",e);
                }
            }
            "git show-staging" =>
            {
                if let Err(e) = show_staging_area()
                {
                    eprintln!("Eroare la afisarea fisierelor in staging:{}",e);
                }
            }
            "git diff" => {
                if let Err(e) = git_diff_with_previous() {
                    eprintln!("Eroare la afisarea diferentelor: {}", e);
                }
            }
            comanda if comanda.starts_with("git diff branch") => {
                let parts: Vec<&str> = comanda.split_whitespace().collect();
                if parts.len() == 5 
                {
                    if let Err(e) = git_diff_branches(parts[3], parts[4]) 
                    {
                        eprintln!("Eroare la comparare branch-uri: {}", e);
                    }
                } 
                else 
                {
                    println!("Usage: git diff branch <branch1> <branch2>");
                }
            }
            comanda if comanda.starts_with("git merge ") => {
                let parts: Vec<&str> = comanda.split_whitespace().collect();
                if parts.len() != 3 
                { 
                    println!("Usage: git merge <branch_name>");
                    continue;
                }
                let source_branch = parts[2];
                if let Err(e) = git_merge(source_branch) 
                {
                    eprintln!("Eroare la merge: {}", e);
                }
            }
            "help"=>
            {
                println!("Comenzi disponibile:");
                println!(" git init                      - Initializeaza un repository nou");
                println!(" git add <filename>            - Adauga un fisier in staging area");
                println!(" git status                    - Afiseaza statusul fisierelor");
                println!(" git commit -m                 - Creeaza un commit cu fisierele din staging area");
                println!(" git branch --show-current     - Afiseaza branch-ul curent");
                println!(" git diff                      - Compara commit-ul curent cu cel commit-ul anterior");
                println!(" git diff branch <b1> <b2>     - Compara două branch-uri (ultimul commit de pe fiecare branch");
                println!(" git merge <branch>            - Face merge între branch-uri");
                println!(" help                          - Afiseaza lista de comenzi"); 
                println!(" exit                          - Iesi din aplicatie");
            }
           "exit" => 
            {
                println!("Ai iesit din aplicatia my_svn. Te asteptam data viitoare!");
                break;
            }
            "" => continue,
            _ => 
            {
                println!("Comanda necunoscuta: '{}'",comanda);
            }
        }
    }
}