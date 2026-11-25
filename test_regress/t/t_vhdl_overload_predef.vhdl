-- DESCRIPTION: Verilator: VHDL predefined function called where same-named overloads do not take the actuals
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3,
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package predef_pkg is
  type slv_arr_t is array (natural range <>) of std_logic_vector(3 downto 0);
  -- Named like numeric_std's to_unsigned, but with one formal
  function to_unsigned(arr : slv_arr_t) return unsigned;
  -- Named like numeric_std's resize, but for bit and logic vectors only
  function resize(vec : bit_vector; length : natural; fill : bit := '0') return bit_vector;
  function resize(vec : std_logic_vector; length : natural; fill : std_logic := '0')
    return std_logic_vector;
  function make_u(value : integer; width : positive) return unsigned;
  -- Named like the to_int overloads of a PoC-style package
  function to_int(sl : std_logic; zero : integer := 0; one : integer := 1) return integer;
  function to_int(flag : boolean; zero : integer := 0; one : integer := 1) return integer;
end package predef_pkg;

package body predef_pkg is
  function to_unsigned(arr : slv_arr_t) return unsigned is
  begin
    return to_unsigned(arr'length, 4);
  end function;

  function resize(vec : bit_vector; length : natural; fill : bit := '0') return bit_vector is
  begin
    return (length - 1 downto 0 => fill);
  end function;

  function resize(vec : std_logic_vector; length : natural; fill : std_logic := '0')
    return std_logic_vector is
  begin
    return (length - 1 downto 0 => fill);
  end function;

  -- The call to to_unsigned is resolved in this body, where the package's own
  -- overload is visible too
  function make_u(value : integer; width : positive) return unsigned is
    variable res : unsigned(width - 1 downto 0);
  begin
    res := to_unsigned(value, res'length);
    return res;
  end function;

  function to_int(sl : std_logic; zero : integer := 0; one : integer := 1) return integer is
  begin
    if sl = '1' then
      return one;
    end if;
    return zero;
  end function;

  function to_int(flag : boolean; zero : integer := 0; one : integer := 1) return integer is
  begin
    if flag then
      return one;
    end if;
    return zero;
  end function;
end package body predef_pkg;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.predef_pkg.all;

entity t is
  port (
    o_direct : out std_logic_vector(7 downto 0);
    o_inline : out std_logic_vector(7 downto 0);
    o_resize : out std_logic_vector(7 downto 0);
    o_toint  : out integer;
    o_bool   : out integer
  );
end entity t;

architecture rtl of t is
  signal u4    : unsigned(3 downto 0) := "0110";
  signal Value : std_logic_vector(3 downto 0) := "1011";
begin
  o_direct <= std_logic_vector(to_unsigned(3, 8));
  o_inline <= std_logic_vector(make_u(5, 8));
  o_resize <= std_logic_vector(resize(u4, 8));

  -- Each element is a std_logic, so only the std_logic overload takes it
  process (Value)
    variable ones : integer;
  begin
    ones := 0;
    for i in Value'range loop
      ones := ones + to_int(Value(i));
    end loop;
    o_toint <= ones;
  end process;

  o_bool <= to_int(Value(0) = '1', 2, 7);
end architecture rtl;
