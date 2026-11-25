-- DESCRIPTION: Verilator: static function loops with exit, next, if, case and element writes
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- Every constant is checked against the value GHDL computes

entity t is end entity;
architecture a of t is
  type iarr is array (0 to 15) of integer;
  -- Labeled and unlabeled exit/next in nested loops, one of them inside an if
  function f_nest return integer is
    variable acc : integer := 0;
  begin
    outer : for i in 0 to 6 loop
      next outer when i = 3;
      for j in 0 to 6 loop
        exit when j > i;
        next when (i + j) mod 2 = 1;
        if (i * j) mod 5 = 4 then
          next;
        end if;
        if i = 6 and j = 4 then
          exit outer;
        end if;
        acc := acc + i * 10 + j;
      end loop;
    end loop;
    return acc;
  end function;
  -- next inside an if skips the rest of the body
  function f_next_if return integer is
    variable acc : integer := 0;
  begin
    for i in 0 to 5 loop
      if i mod 2 = 0 then
        next;
      end if;
      acc := acc + i;
    end loop;
    return acc;
  end function;
  -- A case per iteration, one with a range choice and one on a bit with no others
  function f_case return integer is
    variable acc : integer := 0;
    variable b : bit := '0';
  begin
    for i in 0 to 20 loop
      case i mod 4 is
        when 0 => acc := acc + 1;
        when 1 | 2 => acc := acc * 2;
        when others => acc := acc - 1;
      end case;
      case i is
        when 5 => acc := acc - 3;
        when 7 to 9 => acc := acc + 100;
        when others => null;
      end case;
      case b is
        when '0' => b := '1';
        when '1' => b := '0'; acc := acc + 1000;
      end case;
    end loop;
    return acc;
  end function;
  -- Element writes that read earlier elements back, a nested while, a reverse range
  function f_arr return iarr is
    variable a : iarr := (others => 0);
    variable k : integer;
  begin
    for i in a'range loop
      if i < 2 then
        a(i) := i + 1;
      elsif i mod 2 = 0 then
        a(i) := a(i - 1) + a(i - 2);
      else
        k := 1;
        while k < i loop
          k := k * 2 + 1;
        end loop;
        a(i) := a(i - 1) - k;
      end if;
    end loop;
    for i in a'reverse_range loop
      exit when i < 12;
      a(i) := a(i) * 3;
    end loop;
    return a;
  end function;
  -- A return from inside a loop with a wide range
  function f_find return integer is
  begin
    for i in 0 to 100000 loop
      if i * i > 5000 then
        return i;
      end if;
    end loop;
    return -1;
  end function;
  -- A while loop, and a bare loop of more iterations than an unrolled loop allows
  function f_while return integer is
    variable k : integer := 0;
    variable n : integer := 0;
    variable c : integer := 0;
  begin
    while k < 1000 loop
      k := k * 3 + 1;
      n := n + 1;
    end loop;
    loop
      c := c + 3;
      exit when c >= 30000;
    end loop;
    return n * 100000 + k + c;
  end function;
  -- The loop parameter hides the local i only inside the loop
  function f_hide return integer is
    variable i : integer := 100;
    variable acc : integer := 0;
  begin
    for i in 0 to 3 loop
      acc := acc + i;
    end loop;
    return acc + i;
  end function;
  constant NEST : integer := f_nest;
  constant NEXT_IF : integer := f_next_if;
  constant CASEV : integer := f_case;
  constant ARR : iarr := f_arr;
  constant FIND : integer := f_find;
  constant WHILEV : integer := f_while;
  constant HIDE : integer := f_hide;
begin
  process
  begin
    wait for 1 ns;
    assert NEST = 504 report "nest" severity failure;
    assert NEXT_IF = 9 report "next_if" severity failure;
    assert CASEV = 1039640 report "case" severity failure;
    assert ARR = (1, 2, 3, 0, 3, -4, -1, -8, -9, -24, -33, -48, -243, -288, -531, -576)
      report "arr" severity failure;
    assert FIND = 71 report "find" severity failure;
    assert WHILEV = 731093 report "while" severity failure;
    assert HIDE = 106 report "hide" severity failure;
    std.env.finish;
    wait;
  end process;
end architecture;
