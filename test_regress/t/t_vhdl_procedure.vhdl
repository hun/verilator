-- DESCRIPTION: Verilator: VHDL procedure calls with in/out/inout parameters
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee; use ieee.std_logic_1164.all;

package procp is
  procedure incr(variable v : inout integer; constant by : in integer := 1);
end;

package body procp is
  procedure incr(variable v : inout integer; constant by : in integer := 1) is
  begin
    v := v + by;
  end;
end;

library ieee;
use ieee.std_logic_1164.all; use ieee.numeric_std.all; use work.procp.all;

entity t is
  port (
    clk : in std_logic;
    a : in integer; b : in integer; sel : in integer range 0 to 2;
    y : out integer; z : out integer; q : out integer; c : out integer;
    w : out integer; v : out integer; u : out integer; r2 : out integer; s : out integer;
    cr : out integer; cy : out integer
  );
end;

architecture rtl of t is
  procedure calc(constant x : in integer;
                 constant w : in integer;
                 constant s : in integer;
                 variable r : out integer;
                 variable acc : inout integer) is
    variable t : integer;
  begin
    t := x + w;
    case s is
      when 0 to 0 => r := t;
      when 1 to 1 => r := x - w;
      when others => r := t * 2;
    end case;
    if t > 10 then
      acc := acc + t;
      return;
    end if;
    incr(acc);
    acc := acc + 1;
  end;

  procedure pass(signal i : in integer; signal o : out integer) is
  begin
    o <= i + 1;
  end;

  procedure bump(signal s : inout integer; constant d : in integer) is
  begin
    s <= s + d;
  end;

  procedure sumto(constant n : in integer; variable r : out integer;
                  variable acc : inout integer) is
    variable s : integer := n + 1;
  begin
    r := 0;
    for k in 1 to 3 loop
      r := r + k;
    end loop;
    acc := acc + s;
  end;

  procedure outer(constant x : in integer; variable r : out integer) is
    function f(constant d : in integer) return integer is
    begin
      return d + x;
    end;
    variable acc : integer := f(0);
    procedure inner(constant d : in integer) is
    begin
      acc := acc + f(d);
    end;
  begin
    inner(1);
    inner(2);
    r := acc;
  end;

  signal sc : integer;
  signal cnt : integer := 0;
begin
  pass(a, sc);
  q <= sc;

  with a select cr <=
    10 when 0 to 2,
    20 when 3 to 5,
    30 when others;

  process(a)
    variable cv : integer;
  begin
    case a is
      when 0 to 2 => cv := 1;
      when 3 to 5 => cv := 2;
      when others => cv := 3;
    end case;
    cy <= cv;
  end process;

  process(clk)
    variable acc : integer := 0;
    variable r : integer;
  begin
    if rising_edge(clk) then
      acc := acc + 1;
      calc(x => a, w => b, s => sel, r => r, acc => acc);
      incr(acc);
      incr(by => 2, v => acc);
      y <= r;
      z <= acc;
      bump(cnt, 1);
      c <= cnt;
    end if;
  end process;

  process(clk)
    variable sa : integer := 0;
    variable sr : integer;
  begin
    if rising_edge(clk) then
      sumto(sel + 1, sr, sa);
      w <= sr;
      v <= sa;
    end if;
  end process;

  process(clk)
    variable ur : integer;
  begin
    if rising_edge(clk) then
      outer(sel, ur);
      u <= ur;
    end if;
  end process;

  process(clk)
    variable pv : integer := 0;
    procedure addpv(constant d : in integer) is
    begin
      pv := pv + d;
    end;
  begin
    if rising_edge(clk) then
      addpv(sel + 1);
      r2 <= pv;
    end if;
  end process;

  process(clk)
    variable sq : integer := 0;
    procedure sb(variable r : out integer) is
    begin
      r := sq;
    end;
    procedure sa(variable r : out integer) is
      variable sq : integer := 100;
    begin
      sb(r);
    end;
  begin
    if rising_edge(clk) then
      sa(sq);
      s <= sq;
    end if;
  end process;
end;
