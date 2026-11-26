-- DESCRIPTION: Verilator: wide case choice ranges in functions and selected assignments
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.numeric_std.all;

entity t is
  port (x   : in signed(31 downto 0);
        y   : out integer;
        z   : out integer;
        w   : out integer;
        sel : out integer;
        v   : out integer;
        p   : out integer;
        lp  : out integer;
        k   : out integer);
end entity t;

architecture rtl of t is
  subtype high_t is integer range 100 to integer'high;
  subtype low_t  is integer range integer'low to -100;

  -- Every arm returns
  function by_return (n : integer) return integer is
  begin
    case n is
      when high_t => return 1;
      when low_t  => return -1;
      when 0 to 9 => return 5;
      when others => return 0;
    end case;
  end function by_return;

  -- The arms assign and the function returns afterwards
  function by_assign (n : integer) return integer is
    variable r : integer := 0;
  begin
    case n is
      when high_t => r := 10;
      when low_t  => r := -10;
      when others => r := 3;
    end case;
    return r;
  end function by_assign;

  -- An early return before the case, so the rest of the body follows each arm
  function by_early (n : integer) return integer is
  begin
    if n = 7 then
      return 77;
    end if;
    case n is
      when high_t => return 1;
      when others => return 2;
    end case;
  end function by_early;

  -- A procedure
  procedure classify (n : in integer; r : out integer) is
  begin
    case n is
      when high_t => r := 11;
      when low_t  => r := 22;
      when others => r := 33;
    end case;
  end procedure classify;

  -- A case in a loop of a function: the arm is picked from the loop's own values
  function by_loop (n : integer) return integer is
    variable r : integer := 0;
  begin
    for i in 0 to 2 loop
      case n + i is
        when high_t => r := r + 1;
        when others => r := r + 10;
      end case;
    end loop;
    return r;
  end function by_loop;

  -- A statically known argument picks the arm at elaboration
  constant FIXED : integer := by_return(2147483647) + by_return(-2147483647 - 1)
                              + by_return(4) + by_return(50);
  -- A loop run at elaboration, each arm picked from known values: 3 + 30
  constant LOOPED : integer := by_loop(150) + by_loop(-5);
begin
  y <= by_return(to_integer(x));
  z <= by_assign(to_integer(x));
  v <= by_early(to_integer(x));
  lp <= by_loop(to_integer(x));
  k <= FIXED + LOOPED;

  process (x)
    variable r : integer;
  begin
    classify(to_integer(x), r);
    p <= r;
  end process;

  process (x)
  begin
    case to_integer(x) is
      when high_t => w <= 7;
      when integer'low to -1 => w <= 8;
      when others => w <= 9;
    end case;
  end process;

  with to_integer(x) select
    sel <= 100 when high_t,
           200 when low_t,
           300 when others;
end architecture rtl;
