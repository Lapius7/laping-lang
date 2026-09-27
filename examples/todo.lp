# TODO 管理アプリ（CLI）
#   laping examples/todo.lp add "牛乳を買う"
#   laping examples/todo.lp list
#   laping examples/todo.lp done 1
#   laping examples/todo.lp clear
#
# record・else による既定値・match・table・JSON 保存を使った例

SAVE_FILE = "todo.json"

record Todo(title, done = false)

fn load() {
    return [] unless file_exists(SAVE_FILE)
    return from_json(read_file(SAVE_FILE)) -> map(fn t => Todo(t.title, t.done))
}

fn save(todos) => write_file(SAVE_FILE, to_json(todos, 2))

fn Todo.mark(t) => t.done then color("✓ 完了", "green") else color("・未完了", "yellow")

fn show(todos) {
    if is_empty(todos) {
        box("TODO はありません\nadd \"やること\" で追加できます", "TODO")
        return
    }
    table([[i + 1, t.title, t.mark()] for i, t in todos], ["番号", "やること", "状態"])
    finished = todos -> count(fn t => t.done)
    print("進み具合 {progress(finished, len(todos), 20)}")
}

command = args[0] else "list"
todos = load()

match command {
    "add" => {
        title = args[1] else throw "追加する内容を書いてください: add \"やること\""
        todos.push(Todo(title))
        save(todos)
        print(color("追加しました:", "green"), title)
    }
    "done" => {
        n = num(args[1] else "0")
        throw "番号 {n} の TODO はありません" unless 1 <= n <= len(todos)
        todos[n - 1].done = true
        save(todos)
        print(color("完了にしました:", "green"), todos[n - 1].title)
    }
    "clear" => {
        save(todos -> filter(fn t => not t.done))
        print("完了した TODO を消しました")
    }
    "list" => show(todos)
    _ => print("使い方: laping todo.lp [list | add \"内容\" | done 番号 | clear]")
}
