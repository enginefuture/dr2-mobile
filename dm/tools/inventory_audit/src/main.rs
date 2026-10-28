use d2s::{D2Save, ItemMode, StorePage};
use std::{collections::HashMap, env, fs, process};

fn check_cells(cells: impl IntoIterator<Item = (u8, u8)>) -> Result<usize, String> {
    let mut seen = HashMap::new();
    let mut problems = 0;
    let mut count = 0;
    for cell in cells {
        count += 1;
        if cell.0 >= 14 || cell.1 >= 6 {
            eprintln!("背包物品坐标越界：({}, {})", cell.0, cell.1);
            problems += 1;
        }
        if seen.insert(cell, ()).is_some() {
            eprintln!("背包多个物品占用同一格：({}, {})", cell.0, cell.1);
            problems += 1;
        }
    }
    if problems != 0 { Err(format!("发现 {problems} 处背包坐标异常；请保留备份，不自动修复")) } else { Ok(count) }
}

fn audit(data: &[u8]) -> Result<(), String> {
    if data.len() < 16 || !D2Save::verify_checksum(data) {
        return Err("角色存档校验和不匹配或文件过短；不能判断背包内容".into());
    }
    let save = D2Save::from_bytes(data).map_err(|e| format!("存档解析失败，不支持或数据不完整：{e}"))?;
    if save.file_size as usize != data.len() {
        return Err("存档记录的文件长度不匹配".into());
    }
    let rebuilt = save.to_bytes().map_err(|e| format!("无法验证解析完整性：{e}"))?;
    if rebuilt != data {
        return Err("此存档不能完整往返解析，无法可靠校验背包；原文件未修改".into());
    }
    let cells = save.items.iter().filter_map(|item| {
        let pos = item.position;
        (pos.mode == ItemMode::Stored && pos.store_page == StorePage::Inventory)
            .then_some((pos.inv_x, pos.inv_y))
    });
    let count = check_cells(cells)?;
    println!("角色文件校验和及解析完整，背包 {count} 件：14×6 坐标无重复或越界。此检查不能验证显示、物品属性、储藏箱或实际游戏载入。");
    Ok(())
}

fn main() {
    let mut args = env::args_os().skip(1);
    let path = match (args.next(), args.next()) {
        (Some(path), None) => path,
        _ => { eprintln!("用法：dr2-inventory-audit <角色存档.d2s>（只读）"); process::exit(2); }
    };
    let result = fs::read(path).map_err(|e| format!("无法读取存档：{e}")).and_then(|b| audit(&b));
    if let Err(e) = result { eprintln!("{e}"); process::exit(1); }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn coordinates_are_bounded_and_unique() {
        assert_eq!(check_cells([(0, 0), (13, 5)]), Ok(2));
        assert!(check_cells([(14, 0)]).is_err());
        assert!(check_cells([(0, 6)]).is_err());
        assert!(check_cells([(1, 1), (1, 1)]).is_err());
    }
    #[test]
    fn damaged_or_short_save_is_not_accepted() {
        assert!(audit(&[]).is_err());
        assert!(audit(&[0; 16]).is_err());
    }
}
