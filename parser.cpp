#include "parser.hpp"
#include <sstream>
#include "scanner.hpp"
#include <algorithm>

//Выполняем синтаксический разбор блока program. Если во время разбора не обнаруживаем
//никаких ошибок, то выводим последовательность команд стек-машины

void Parser::parse()
{
  program();
  if (!error_)
  {
    codegen_->flush();
  }
}

void Parser::program()
{
  mustBe(T_BEGIN);
  statementList();
  mustBe(T_END);
  codegen_->emit(STOP);
}

void Parser::statementList()
{
  //	  Если список операторов пуст, очередной лексемой будет одна из возможных "закрывающих скобок": END, OD, ELSE, FI.
  //	  В этом случае результатом разбора будет пустой блок (его список операторов равен null).
  //	  Если очередная лексема не входит в этот список, то ее мы считаем началом оператора и вызываем метод statement.
  //    Признаком последнего оператора является отсутствие после оператора точки с запятой.

  if (see(T_END) || see(T_OD) || see(T_ELSE) || see(T_FI))
  {}
  else
  {
    bool more = true;
    while (more)
    {
      statement();
      more = match(T_SEMICOLON);
    }
  }
}

void Parser::statement()
{
  // Если встречаем переменную, то запоминаем ее адрес или добавляем новую если не встретили.
  // Следующей лексемой должно быть присваивание. Затем идет блок expression, который возвращает значение на вершину стека.
  // Записываем это значение по адресу нашей переменной

  if (match(T_INT))
  {
    mustBe(T_IDENTIFIER);
    int varAddress = addVariable(scanner_->getStringValue());
    mustBe(T_ASSIGN);
    expression();
    codegen_->emit(STORE, varAddress);
  }
  else if (match(T_FLOAT))
  {
    mustBe(T_IDENTIFIER);
    int varAddress = addVariable(scanner_->getStringValue(), true);
    mustBe(T_ASSIGN);
    expression();
    codegen_->emit(STORE, varAddress);
  }

  else if (match(T_IDENTIFIER))
  {
    int varAddress = findVariable(scanner_->getStringValue());
    mustBe(T_ASSIGN);
    expression();
    codegen_->emit(STORE, varAddress);
  }
    // Если встретили IF, то затем должно следовать условие. На вершине стека лежит 1 или 0 в зависимости от выполнения условия.
    // Затем зарезервируем место для условного перехода JUMP_NO к блоку ELSE (переход в случае ложного условия). Адрес перехода
    // станет известным только после того, как будет сгенерирован код для блока THEN.
  else if (match(T_IF))
  {
    relation();

    int jumpNoAddress = codegen_->reserve();

    mustBe(T_THEN);
    statementList();
    if (match(T_ELSE))
    {
      //Если есть блок ELSE, то чтобы не выполнять его в случае выполнения THEN,
      //зарезервируем место для команды JUMP в конец этого блока
      int jumpAddress = codegen_->reserve();
      //Заполним зарезервированное место после проверки условия инструкцией перехода в начало блока ELSE.
      codegen_->emitAt(jumpNoAddress, JUMP_NO, codegen_->getCurrentAddress());
      statementList();
      //Заполним второй адрес инструкцией перехода в конец условного блока ELSE.
      codegen_->emitAt(jumpAddress, JUMP, codegen_->getCurrentAddress());
    }
    else
    {
      //Если блок ELSE отсутствует, то в зарезервированный адрес после проверки условия будет записана
      //инструкция условного перехода в конец оператора IF...THEN
      codegen_->emitAt(jumpNoAddress, JUMP_NO, codegen_->getCurrentAddress());
    }

    mustBe(T_FI);
  }

  else if (match(T_WHILE))
  {
    //запоминаем адрес начала проверки условия.
    int conditionAddress = codegen_->getCurrentAddress();
    relation();
    //резервируем место под инструкцию условного перехода для выхода из цикла.
    int jumpNoAddress = codegen_->reserve();
    mustBe(T_DO);
    statementList();
    mustBe(T_OD);
    //переходим по адресу проверки условия
    codegen_->emit(JUMP, conditionAddress);
    //заполняем зарезервированный адрес инструкцией условного перехода на следующий за циклом оператор.
    codegen_->emitAt(jumpNoAddress, JUMP_NO, codegen_->getCurrentAddress());
  }
  else if (match(T_WRITE))
  {
    mustBe(T_LPAREN);
    expression();
    mustBe(T_RPAREN);
    codegen_->emit(PRINT);
  }
  else
  {
    reportError("statement expected.");
  }
}

void Parser::expression()
{

  /*
        Арифметическое выражение описывается следующими правилами: <expression> -> <term> | <term> + <term> | <term> - <term>
        При разборе сначала смотрим первый терм, затем анализируем очередной символ. Если это '+' или '-',
    удаляем его из потока и разбираем очередное слагаемое (вычитаемое). Повторяем проверку и разбор очередного
    терма, пока не встретим за термом символ, отличный от '+' и '-'
    */

  term();
  while (see(T_ADDOP))
  {
    Arithmetic op = scanner_->getArithmeticValue();
    next();
    term();

    if (op == A_PLUS)
    {
      codegen_->emit(ADD);
    }
    else
    {
      codegen_->emit(SUB);
    }
  }
}

void Parser::term()
{
  /*
    Терм описывается следующими правилами: <expression> -> <factor> | <factor> + <factor> | <factor> - <factor>
        При разборе сначала смотрим первый множитель, затем анализируем очередной символ. Если это '*' или '/',
    удаляем его из потока и разбираем очередное слагаемое (вычитаемое). Повторяем проверку и разбор очередного
    множителя, пока не встретим за ним символ, отличный от '*' и '/'
 */
  factor();
  while (see(T_MULOP))
  {
    Arithmetic op = scanner_->getArithmeticValue();
    next();
    factor();

    if (op == A_MULTIPLY)
    {
      codegen_->emit(MULT);
    }
    else
    {
      codegen_->emit(DIV);
    }
  }
}

void Parser::factor()
{
  /*
    Множитель описывается следующими правилами:
    <factor> -> number | identifier | -<factor> | (<expression>) | READ
  */
  if (see(T_NUMBER))
  {
    next();
    if(see(T_ADDOP) || see(T_MULOP) || see(T_CMP))
    {
      if(!isFloatCast.empty())
      {
        if(isFloatCast.front())
        {
          codegen_->emit(PUSH, static_cast<float>(scanner_->getIntValue()));
        }
      }
      else codegen_->emit(PUSH, scanner_->getIntValue());
    }
    else if(lastVar_.second)
    {
      codegen_->emit(PUSH, static_cast<float>(scanner_->getIntValue()));
    }
    else codegen_->emit(PUSH, scanner_->getIntValue());
    isFloatCast.erase(isFloatCast.begin(), isFloatCast.end());
  }
  else if(see(T_RNUMBER))
  {
    if (lastToken_ == T_MULOP || lastToken_ == T_ADDOP || lastToken_ == T_CMP)
    {
      codegen_->emit(PUSH, scanner_->getFloatValue());
      next();
      return;
    }
    next();
    if (see(T_ADDOP) || see(T_MULOP) || see(T_CMP))
    {
      if (!isFloatCast.empty())
      {
        int val = 0;
        if (any_of(isFloatCast.begin(), isFloatCast.end(), [] (bool s) { return !s;}))
        {
          val = static_cast<int>(scanner_->getFloatValue());
          if (isFloatCast.front())
          {
            codegen_->emit(PUSH, static_cast<float>(val));
          }
          else codegen_->emit(PUSH, val);
        }
      }
      else codegen_->emit(PUSH, scanner_->getFloatValue());
    }
    else if (lastVar_.second)
    {
      int val = 0;
      if (any_of(isFloatCast.begin(), isFloatCast.end(), [](bool s) { return !s; }))
      {
        val = static_cast<int>(scanner_->getFloatValue());
        codegen_->emit(PUSH, static_cast<float>(val));
      }
      else codegen_->emit(PUSH, scanner_->getFloatValue());
    }
    else codegen_->emit(PUSH, static_cast<int>(scanner_->getFloatValue()));
    isFloatCast.erase(isFloatCast.begin(), isFloatCast.end());
  }

    //Если встретили число, то записываем на вершину стека

  else if (see(T_IDENTIFIER))
  {
    int varAddress = findVariable(scanner_->getStringValue());
    next();
    codegen_->emit(LOAD, varAddress);
    //Если встретили переменную, то выгружаем значение, лежащее по ее адресу, на вершину стека
  }
  else if (see(T_ADDOP) && scanner_->getArithmeticValue() == A_MINUS)
  {
    next();
    factor();
    codegen_->emit(INVERT);
    //Если встретили знак "-", и за ним <factor> то инвертируем значение, лежащее на вершине стека
  }
  else if (match(T_LPAREN))
  {
    if(see(T_INT))
    {
      next();
      isFloatCast.push_back(false);
      mustBe(T_RPAREN);
      expression();
    }
    else if(see(T_FLOAT))
    {
      next();
      isFloatCast.push_back(true);
      mustBe(T_RPAREN);
      expression();
    }
    else
    {
      isFloatCast.erase(isFloatCast.begin(), isFloatCast.end());
      expression();
      mustBe(T_RPAREN);
    }
    //Если встретили открывающую скобку, тогда следом может идти любое арифметическое выражение и обязательно
    //закрывающая скобка.
  }
  else if (match(T_READ))
  {
    codegen_->emit(INPUT);
    //Если встретили зарезервированное слово READ, то записываем на вершину стека идет запись со стандартного ввода
  }
  else
  {
    reportError("expression expected.");
  }
}

void Parser::relation()
{
  //Условие сравнивает два выражения по какому-либо из знаков. Каждый знак имеет свой номер. В зависимости от
  //результата сравнения на вершине стека окажется 0 или 1.
  expression();
  if (see(T_CMP))
  {
    Cmp cmp = scanner_->getCmpValue();
    next();
    expression();
    switch (cmp)
    {
      //для знака "=" - номер 0
    case C_EQ:
      codegen_->emit(COMPARE, 0);
      break;
      //для знака "!=" - номер 1
    case C_NE:
      codegen_->emit(COMPARE, 1);
      break;
      //для знака "<" - номер 2
    case C_LT:
      codegen_->emit(COMPARE, 2);
      break;
      //для знака ">" - номер 3
    case C_GT:
      codegen_->emit(COMPARE, 3);
      break;
      //для знака "<=" - номер 4
    case C_LE:
      codegen_->emit(COMPARE, 4);
      break;
      //для знака ">=" - номер 5
    case C_GE:
      codegen_->emit(COMPARE, 5);
      break;
    };
  }
  else
  {
    reportError("comparison operator expected.");
  }
}

int Parser::findVariable(const string& var)
{
  auto it = variables_.find(var);
  if (it == variables_.end())
  {
    reportError("Variable '" + var + "' has not been declared.");
  }
  else
  {
    return it->second.first;
  }
}

int Parser::addVariable(const string& var, bool isFloat)
{
  auto it = variables_.find(var);
  if (it == variables_.end())
  {
    lastVar_.second = isFloat;
    variables_[var] = lastVar_;
  }
  else
  {
    reportError("Variable '" + var + "' has been already declared.");
  }
  return lastVar_.first++;
}

void Parser::mustBe(Token t)
{
  if (!match(t))
  {
    error_ = true;

    // Подготовим сообщение об ошибке
    std::ostringstream msg;
    msg << tokenToString(scanner_->token()) << " found while " << tokenToString(t) << " expected.";
    reportError(msg.str());

    // Попытка восстановления после ошибки.
    recover(t);
  }
}

void Parser::recover(Token t)
{
  while (!see(t) && !see(T_EOF))
  {
    next();
  }

  if (see(t))
  {
    next();
  }
}
