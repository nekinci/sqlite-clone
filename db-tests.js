
const expect = require('chai').expect
const exec = require('child_process').exec

describe('database-tests', function (){

    async function delete_db_after_test(db){
        const output = await exec('rm -f ' + db)
        return output
    }

    async function row_script(commands = [], expected = []){
        let row_output = []
        const new_db = 'new_db_' + Date.now().valueOf() + '.db'
        const {stdout, stderr, stdin} = await exec('./db_example ' + new_db)
        if (commands.size == 0) stdin.end()
        commands.forEach((command) => {
            stdin.write(command + '\n')
        })

        stdout.on('data', (output) => {
            row_output.push(...output.split('\n'))
        })

        stdin.end()
        stdout.on('close', () => {
            expect(row_output).to.eql(expected)
        })

        await delete_db_after_test(new_db)
        return row_output

    }

    it('cmake build', async function build(){
        const {stdout, stderr} = await exec('cmake --build .')
        stdout.on('data', (data) => {
            expect(data).to.includes('[100%] Built')
        })
    })

    it('insert and retrieves a row', async function insertTest() {
        await row_script([
            'insert 1 user1 person1@example.com',
            'select',
            '.exit'
            ],
            [
            'db > Executed .',
            'db > (1, user1, person1@example.com)',
            'Executed .',
            'db > '
        ])
    })

    it('prints error message when table is full', async function (){
        let arr = Array(1402).fill(0)
        const out_arr = []
        const new_db = 'new_db_' + Date.now().valueOf() + '.db'
        const {stdin, stdout} = exec('./db_example ' + new_db)
        stdout.on('data', function (out){
            out_arr.push(...out.split('\n'))
        })
        stdout.on('close', async function (){
            expect(out_arr[1402]).to.eq('db > Error: Table full.')
            await delete_db_after_test(new_db)
        })

        arr.forEach((x, i) => {
            if (i == 1401){
                stdin.write('.exit\n')
            } else{
                stdin.write(`insert ${i+1} user${i+1} person${i+1}@example.com\n`)
            }
        })

    })

    it('allows inserting strings that are maximum length', function () {
        let username = Array(32).fill(1).map(() => 'a').join('')
        let email = Array(255).fill(1).map(() => 'a').join('')
        row_script([
            `insert 1 ${username} ${email}`,
            'select',
            '.exit'
        ], [
            'db > Executed .',
            `db > (1, ${username}, ${email})`,
            'db > Executed .',
            'db > '
        ])
    })

    it('prints error message if strings are too long', function () {
        let username = Array(33).fill(1).map(() => 'a').join('')
        let email = Array(256).fill(1).map(() => 'a').join('')
        row_script([
            `insert 1 ${username} ${email}`,
            'select',
            '.exit'
        ], [
            'db > String is too long.',
            `db > Executed .`,
            'db > '
        ])
    })

    it('prints error message if id is less than zero', function () {
        row_script([
            'insert -1 csaf bfafas',
            'select',
            '.exit'
        ], [
            'db > ID must be positive.',
            'db > Executed .',
            'db > '
        ])
    })

    it('keeps data after closing connection', function () {
        row_script([
            'insert 1 a b',
            '.exit'
        ], [
            'db > Executed .',
            'db > '
        ])

        row_script([
            'select',
            '.exit'
        ], [
            'db > (1, a, b)',
            'Executed .',
            'db > '
        ])
    })

    it('print constants', function (){
        const script = ['.constants', '.exit']
        const expected = [
            'db > Constants: ',
            'ROW_SIZE: 293',
            'COMMON_NODE_HEADER_SIZE: 6',
            'LEAF_NODE_HEADER_SIZE: 10',
            'LEAF_NODE_CELL_SIZE: 297',
            'LEAF_NODE_SPACE_FOR_CELLS: 4086',
            'LEAF_NODE_MAX_CELLS: 13',
            'db > '
        ]
        row_script(script, expected)
    })

    it('allows printing out the structure of a one-node btree', function (){
        const script = [3, 1, 2].map((value) => `ìnsert ${value} user${value} person${value}@example.com`)
        script.push('.btree')
        script.push('.exit')
        const expected = [
            'db > Executed .',
            'db > Executed .',
            'db > Executed .',
            'db > Tree:',
            'leaf(size 3)',
            '  - 0 : 1',
            '  - 1 : 2',
            '  - 2 : 3',
            'db > '
        ]
        row_script(script, expected)
    })

    it('prints an error message if there is a duplicate id', function () {
        const script = [
            'insert 1 user1 person1@example.com',
            'insert 1 user1 person1@example.com',
            'select',
            '.exit'
        ]

        const expected = [
            'db > Executed .',
            'db > Error: Duplicate key.',
            'db > (1, user1, person1@example.com)',
            'db > Executed .',
            'db > '
        ]

        row_script(script, expected)
    });
})