#include "Debug.h"
#include "Compiler.h"
#include "Disassembler.h"

yo::Compiler::Compiler()
{
	intializeParserRules();
}

bool yo::Compiler::compile(const char* source, Chunk* chunk)
{
	lexer.open(source);
	currentChunk = chunk;

	parser.errorFound = false;
	parser.panicMode = false;

	advance();

	while (!matchToken(TokenType::T_EOF))
		declaration();

	finish();

	return !parser.errorFound;
}

void yo::Compiler::advance()
{
	parser.previous = parser.current;

	while (true)
	{
		parser.current = lexer.nextToken();

		if (parser.current.type != TokenType::T_ERROR)
			break;

		handleErrorAtCurrentToken(parser.current.data);
	}
}

void yo::Compiler::declaration()
{
	if (matchToken(TokenType::T_VAR))
		variableDeclaration();
	else
		statement();
}

void yo::Compiler::variableDeclaration()
{
	uint8_t globalVariable = parseVariable("Expected a variable name");

	if (matchToken(TokenType::T_EQUAL))
		expression();
	else
		emitByte((uint8_t)OPCode::OP_NONE);

	eat(TokenType::T_SEMICOLON, "Expected ';' after expression");

	defineVariable(globalVariable);
}

void yo::Compiler::statement()
{
	if (matchToken(TokenType::T_PRINT))
		statementPrint();
	else if (matchToken(TokenType::T_IF))
		statementIf();
	else if (matchToken(TokenType::T_WHILE))
		statementWhile();
	else if (matchToken(TokenType::T_FOR))
		statementFor();
	else if (matchToken(TokenType::T_LEFT_BRACES))
	{
		startScope();
		scopeBlock();
		endScope();
	}
	else
		statementExpression();
}

void yo::Compiler::expression()
{
	parsePrecedence(Precedence::P_ASSIGNMENT);
}

void yo::Compiler::eat(TokenType type, const char* message)
{
	if (parser.current.type == type)
	{
		advance();
		return;
	}

	handleErrorAtCurrentToken(message);
}

void yo::Compiler::finish()
{
	emitByte((uint8_t)OPCode::OP_RETURN);

	#ifdef DEBUG_COMPILER_TRACE
	if (!parser.errorFound)
		Disassembler::disassemble(*currentChunk, "Compiler");
	#endif
}

void yo::Compiler::grouping()
{
	expression();
	eat(TokenType::T_RIGHT_PARENTHESIS, "Expected ')' after expression");
}

void yo::Compiler::startScope()
{
	++localStack.scopeDepth;
}

void yo::Compiler::scopeBlock()
{
	while (!checkToken(TokenType::T_RIGHT_BRACES) && !checkToken(TokenType::T_EOF))
		declaration();

	eat(TokenType::T_RIGHT_BRACES, "Expected '}' after declaration");
}

void yo::Compiler::endScope()
{
	--localStack.scopeDepth;

	while (localStack.locals.size() > 0 && (unsigned int)localStack.locals.back().depth > localStack.scopeDepth)
	{
		emitByte((uint8_t)OPCode::OP_POP_BACK);
		localStack.locals.pop_back();
	}
}

void yo::Compiler::statementExpression()
{
	expression();

	eat(TokenType::T_SEMICOLON, "Expected ';' after expression");

	emitByte((uint8_t)OPCode::OP_POP_BACK);
}

void yo::Compiler::statementPrint()
{
	eat(TokenType::T_LEFT_PARENTHESIS, "Expected a '('");

	expression();

	eat(TokenType::T_RIGHT_PARENTHESIS, "Expected a ')'");

	eat(TokenType::T_SEMICOLON, "Expected ';' after expression");

	emitByte((uint8_t)OPCode::OP_PRINT);
}

void yo::Compiler::statementIf()
{
	eat(TokenType::T_LEFT_PARENTHESIS, "Expected a '('");
	
	expression();

	eat(TokenType::T_RIGHT_PARENTHESIS, "Expected a ')'");

	int thenJump = emitJump((uint8_t)OPCode::OP_JUMP_IF_FALSE);

	emitByte((uint8_t)OPCode::OP_POP_BACK);

	statement();

	int elseJump = emitJump((uint8_t)OPCode::OP_JUMP);

	patchJump(thenJump);

	emitByte((uint8_t)OPCode::OP_POP_BACK);

	if (matchToken(TokenType::T_ELSE))
		statement();

	patchJump(elseJump);
}

void yo::Compiler::statementWhile()
{
	int loopStart = currentChunk->data.size();

	eat(TokenType::T_LEFT_PARENTHESIS, "Expected a '('");
	expression();
	eat(TokenType::T_RIGHT_PARENTHESIS, "Expected a ')'");

	int exitJump = emitJump((uint8_t)OPCode::OP_JUMP_IF_FALSE);
	emitByte((uint8_t)OPCode::OP_POP_BACK);
	statement();

	emitLoop(loopStart);

	patchJump(exitJump);
	emitByte((uint8_t)OPCode::OP_POP_BACK);
}

void yo::Compiler::statementFor()
{
	startScope();

	eat(TokenType::T_LEFT_PARENTHESIS, "Expected a '('");

	if (matchToken(TokenType::T_SEMICOLON))
		{ }
	else if (matchToken(TokenType::T_VAR))
		variableDeclaration();
	else
		statementExpression();

	int loopStart = currentChunk->data.size();
	int exit = -1;

	if (!matchToken(TokenType::T_SEMICOLON))
	{
		expression();
		eat(TokenType::T_SEMICOLON, "Expected a ';' after loop condition");

		exit = emitJump((uint8_t)OPCode::OP_JUMP_IF_FALSE);
		emitByte((uint8_t)OPCode::OP_POP_BACK);
	}
	
	if (!matchToken(TokenType::T_RIGHT_PARENTHESIS)) 
	{
		int bodyJump = emitJump((uint8_t)OPCode::OP_JUMP);
		int incrementStart = currentChunk->data.size();

		expression();

		emitByte((uint8_t)OPCode::OP_POP_BACK);

		eat(TokenType::T_RIGHT_PARENTHESIS, "Expected a ')'");

		emitLoop(loopStart);

		loopStart = incrementStart;

		patchJump(bodyJump);
	}

	statement();
	emitLoop(loopStart);
	
	if (exit != -1)
	{
		patchJump(exit);
		emitByte((uint8_t)OPCode::OP_POP_BACK);
	}

	endScope();
}

uint8_t yo::Compiler::parseVariable(const char* message)
{
	eat(TokenType::T_IDENTIFIER, message);

	declareVariable();
	if (localStack.scopeDepth > 0)
		return 0;

	return identifierConstant(&parser.previous);
}

void yo::Compiler::defineVariable(uint8_t globalVariable)
{
	if (localStack.scopeDepth > 0)
		return markInitialized();

	emitByte((uint8_t)OPCode::OP_DEFINE_GLOBAL_VAR);
	emitByte(globalVariable);
}

void yo::Compiler::declareVariable()
{
	if (localStack.scopeDepth == 0)
		return;

	Token* name = &parser.previous;
	for (int i = localStack.locals.size() - 1; i >= 0; --i)
	{
		LocalVar var = localStack.locals[i];
		if (var.depth != -1 && var.depth < (int)localStack.scopeDepth)
			break;

		if (*name == var.name)
			handleErrorAtCurrentToken("A variable assigned to this name already exists in this scope");
	}

	addLocal(*name);
}

void yo::Compiler::markInitialized()
{
	localStack.locals.back().depth = localStack.scopeDepth;
}

void yo::Compiler::synchronize()
{
	parser.panicMode = false;

	while (parser.current.type != TokenType::T_EOF)
	{
		if (parser.previous.type == TokenType::T_SEMICOLON)
			return;

		switch (parser.current.type)
		{
			case TokenType::T_CLASS:
			case TokenType::T_FUNC:
			case TokenType::T_VAR:
			case TokenType::T_FOR:
			case TokenType::T_IF:
			case TokenType::T_WHILE:
			case TokenType::T_PRINT:
			case TokenType::T_RETURN:
				return;
		}

		advance();
	}
}

void yo::Compiler::emitByte(uint8_t byte)
{
	currentChunk->push_back(byte, parser.previous.line);
}

void yo::Compiler::emitConstant(Value value)
{
	emitByte((uint8_t)OPCode::OP_CONSTANT);
	currentChunk->push_constant(value, parser.previous.line);
}

int yo::Compiler::emitJump(uint8_t instruction)
{
	emitByte(instruction);
	emitByte(0xFF);
	emitByte(0xFF);
	return currentChunk->data.size() - 2;
}

void yo::Compiler::emitLoop(int loopStart)
{
	emitByte((uint8_t)OPCode::OP_LOOP);

	int offset = currentChunk->data.size() - loopStart + 2;
	if (offset > UINT16_MAX)
		handleErrorAtCurrentToken("The previous while offset was too large");

	emitByte((offset >> 8) & 0xff);
	emitByte(offset & 0xff);
}

void yo::Compiler::patchJump(int offset)
{
	int jump = currentChunk->data.size() - offset - 2;

	if (jump > UINT16_MAX)
		handleErrorAtCurrentToken("The previous jump offset was too large");

	currentChunk->data[offset] = (jump >> 8) & 0xFF;
	currentChunk->data[offset + 1] = jump & 0xFF;
}

void yo::Compiler::numeric(bool canAssign)
{
	double value = std::strtod(parser.previous.data.c_str(), NULL);
	emitConstant({ value });
}

void yo::Compiler::unary(bool canAssign)
{
	TokenType type = parser.previous.type;

	parsePrecedence(Precedence::P_UNARY);

	switch (type)
	{
	case TokenType::T_MINUS:
		emitByte((uint8_t)OPCode::OP_NEGATE);
		break;
	case TokenType::T_EXCLAMATION:
		emitByte((uint8_t)OPCode::OP_NOT);
		break;
	}
}

void yo::Compiler::binary(bool canAssign)
{
	TokenType type = parser.previous.type;

	Rule* rule = getParserRule(type);

	parsePrecedence((Precedence)((int)rule->precedence + 1));

	switch (type)
	{
	case TokenType::T_PLUS:
		emitByte((uint8_t)OPCode::OP_ADD);
		break;
	case TokenType::T_MINUS:
		emitByte((uint8_t)OPCode::OP_SUB);
		break;
	case TokenType::T_ASTERISTIC:
		emitByte((uint8_t)OPCode::OP_MULT);
		break;
	case TokenType::T_SLASH:
		emitByte((uint8_t)OPCode::OP_DIV);
		break;
		
	case TokenType::T_EQUAL_EQUAL:
		emitByte((uint8_t)OPCode::OP_EQUAL);
		break;
	case TokenType::T_EXCLAMATION_EQUAL:
		emitByte((uint8_t)OPCode::OP_EQUAL);
		emitByte((uint8_t)OPCode::OP_NOT);
		break;
	case TokenType::T_GREATER:
		emitByte((uint8_t)OPCode::OP_GREATER);
		break;
	case TokenType::T_GREATER_EQUAL:
		emitByte((uint8_t)OPCode::OP_LESS);
		emitByte((uint8_t)OPCode::OP_NOT);
		break;
	case TokenType::T_LESS:
		emitByte((uint8_t)OPCode::OP_LESS);
		break;
	case TokenType::T_LESS_EQUAL:
		emitByte((uint8_t)OPCode::OP_GREATER);
		emitByte((uint8_t)OPCode::OP_NOT);
		break;
	}
}

void yo::Compiler::literalType(bool canAssign)
{
	switch (parser.previous.type)
	{
	case TokenType::T_NONE:
		emitByte((uint8_t)OPCode::OP_NONE);
		break;
	case TokenType::T_TRUE:
		emitByte((uint8_t)OPCode::OP_TRUE);
		break;
	case TokenType::T_FALSE:
		emitByte((uint8_t)OPCode::OP_FALSE);
		break;
	}
}

void yo::Compiler::string(bool canAssign)
{
	std::string str = prepareStringObject();

	emitConstant({ str });
}

void yo::Compiler::variable(bool canAssign)
{
	namedVariable(parser.previous, canAssign);
}

void yo::Compiler::namedVariable(Token name, bool canAssign)
{
	OPCode getOperation, setOperation;
	int arg = resolveLocal(name);

	if (arg != -1)
	{
		getOperation = OPCode::OP_GET_LOCAL_VAR;
		setOperation = OPCode::OP_SET_LOCAL_VAR;
	}
	else
	{
		arg = identifierConstant(&name);
		getOperation = OPCode::OP_GET_GLOBAL_VAR;
		setOperation = OPCode::OP_SET_GLOBAL_VAR;
	}

	if (canAssign && matchToken(TokenType::T_EQUAL))
	{
		expression();
		emitByte((uint8_t)setOperation);
		emitByte((uint8_t)arg);
	}
	else
	{
		emitByte((uint8_t)getOperation);
		emitByte((uint8_t)arg);
	}
}

void yo::Compiler::parsePrecedence(const Precedence& precendece)
{
	advance();
	TokenType type = parser.previous.type;
	std::function<void(bool)> prefix = getParserRule(type)->prefix;

	if (!prefix)
	{
		handleErrorAtCurrentToken("Expected expression");
		return;
	}

	bool canAssign = precendece <= Precedence::P_ASSIGNMENT;
	prefix(canAssign);

	while (precendece <= getParserRule(parser.current.type)->precedence)
	{
		advance();

		std::function<void(bool)> infix = getParserRule(parser.previous.type)->infix;
		infix(canAssign);
	}

	if (canAssign && matchToken(TokenType::T_EQUAL))
		handleErrorAtCurrentToken("Invalid assignment target.");
}

uint8_t yo::Compiler::identifierConstant(Token* name)
{
	currentChunk->push_constant_only({ name->data });
	return (uint8_t)currentChunk->constantPool.size() - 1;
}

int yo::Compiler::resolveLocal(Token name)
{
	for (int i = localStack.locals.size() - 1; i >= 0; i--)
	{
		LocalVar local = localStack.locals[i];
		if (name == local.name)
		{
			if(local.depth == -1)
				handleErrorAtCurrentToken("Unable to read local variable in its own initializer.");
			return i;
		}
	}

	return -1;
}

void yo::Compiler::addLocal(Token name)
{
	localStack.locals.push_back({name, -1});
}

std::string yo::Compiler::prepareStringObject() const
{
	std::string str = parser.previous.data;
	std::string a = parser.current.data;

	return str;
}

//yo::StringObject* yo::Compiler::allocateStringObject(const std::string& str)
//{
//	return new StringObject(str);
//}

void yo::Compiler::intializeParserRules()
{
	parseRules.insert({
		TokenType::T_LEFT_PARENTHESIS,
		Rule(std::bind(&Compiler::grouping, this), nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_RIGHT_PARENTHESIS,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_LEFT_BRACES,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_RIGHT_BRACES,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_COMMA,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_DOT,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_MINUS,
		Rule(std::bind(&Compiler::unary, this, false), std::bind(&Compiler::binary, this, false), Precedence::P_TERM)
	});

	parseRules.insert({
		TokenType::T_PLUS,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_TERM)
	});

	parseRules.insert({
		TokenType::T_SLASH,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_FACTOR)
	});

	parseRules.insert({
		TokenType::T_ASTERISTIC,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_FACTOR)
	});

	parseRules.insert({
		TokenType::T_SEMICOLON,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_EXCLAMATION,
		Rule(std::bind(&Compiler::unary, this, false), nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_EXCLAMATION_EQUAL,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_EQUAL)
	});

	parseRules.insert({
		TokenType::T_EQUAL,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_EQUAL_EQUAL,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_COMPARE)
	});

	parseRules.insert({
		TokenType::T_GREATER,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_COMPARE)
	});

	parseRules.insert({
		TokenType::T_GREATER_EQUAL,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_COMPARE)
	});

	parseRules.insert({
		TokenType::T_LESS,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_COMPARE)
	});

	parseRules.insert({
		TokenType::T_LESS_EQUAL,
		Rule(nullptr, std::bind(&Compiler::binary, this, false), Precedence::P_COMPARE)
	});

	parseRules.insert({
		TokenType::T_IDENTIFIER,
		Rule(std::bind(&Compiler::variable, this, true), nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_STRING,
		Rule(std::bind(&Compiler::string, this, true), nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_NUMERIC,
		Rule(std::bind(&Compiler::numeric, this, true), nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_AND,
		Rule(nullptr, std::bind(&Compiler::andRule, this, false), Precedence::P_AND)
	});

	parseRules.insert({
		TokenType::T_OR,
		Rule(nullptr, std::bind(&Compiler::orRule, this, false), Precedence::P_OR)
	});

	parseRules.insert({
		TokenType::T_IF,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_ELSE,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_FALSE,
		Rule(std::bind(&Compiler::literalType, this, true), nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_TRUE,
		Rule(std::bind(&Compiler::literalType, this, true), nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_FOR,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_WHILE,
		Rule(nullptr, nullptr, Precedence::P_NONE)
		});

	parseRules.insert({
		TokenType::T_NONE,
		Rule(std::bind(&Compiler::literalType, this, true), nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_PRINT,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_VAR,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_FUNC,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_RETURN,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_CLASS,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_SUPER,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_THIS,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_ERROR,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});

	parseRules.insert({
		TokenType::T_EOF,
		Rule(nullptr, nullptr, Precedence::P_NONE)
	});
}

yo::Rule* yo::Compiler::getParserRule(TokenType type)
{
	return &parseRules[type];
}

void yo::Compiler::handleErrorAtCurrentToken(const std::string& message)
{
	handleErrorToken(&parser.current, message);
}

void yo::Compiler::handleErrorToken(Token* token, const std::string& message)
{
	if (parser.panicMode)
		return;

	parser.panicMode = true;

	fprintf(stderr, "<Line %d> Error ", token->line);

	if (token->type == TokenType::T_EOF)
		fprintf(stderr, "at the end of the file");

	else if (token->type == TokenType::T_ERROR) {}

	else
		fprintf(stderr, "at '%.*s'", token->data.length(), token->data.c_str());

	fprintf(stderr, ": %s\n", message.c_str());
	parser.errorFound = true;
}

bool yo::Compiler::matchToken(TokenType type)
{
	if (parser.current.type != type)
		return false;

	advance();

	return true;
}

bool yo::Compiler::checkToken(TokenType type)
{
	return parser.current.type == type;
}
