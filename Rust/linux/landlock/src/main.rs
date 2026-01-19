use std::env;
use std::fs;
use std::path::Path;
use std::ffi::OsStr;
use std::error::Error;

use landlock::{
    Ruleset, RulesetAttr, RulesetCreatedAttr, RulesetStatus,
    AccessFs, path_beneath_rules
};



fn main() -> Result<(), Box<dyn Error>>
{
    let args: Vec<String> = env::args().collect();

    if args.len() <= 1 {
        println!("Usage: {} <filename>\n", &args[0]);
        return Ok(());
    }


    #[cfg(feature = "landlock")]
    {
        let abi = landlock::ABI::V6;

        let Ok(rules) = Ruleset::default().handle_access(AccessFs::from_read(abi)) else {
            return Err("Could not retrieve default Landlock rules, exiting...".into()); };

        let Ok(mut path_rules) = rules.create() else {
            return Err("Could not create custom Landlock rules, exiting...".into()); };

        let path_rules_ref = &mut path_rules;
        let Ok(_) = path_rules_ref.add_rules(path_beneath_rules(
                      vec![OsStr::new("/tmp")], AccessFs::from_read(abi)
                    )) else {
            return Err("Could not add Landlock path rule, exiting...".into()); };

        let Ok(status) = path_rules.restrict_self() else {
            return Err("Could not enforce Landlock policy, exiting...".into()); };

        if let RulesetStatus::NotEnforced { .. } = status.ruleset {
            return Err("Landlock policy was not enforced, exiting...".into()); };
    }


    let path = Path::new(&args[1]);
    if let Ok(_) = fs::File::open(path) {
        println!("Successfully opened '{}'!", &args[1]);
    } else {
        println!("Could not open '{}'...", &args[1]);
    }

    Ok(())
}
