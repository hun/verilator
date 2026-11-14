-- DESCRIPTION: Verilator: VHDL package body sees its specification's declarations
--
-- This file ONLY is placed under the Creative Commons Public Domain.
-- SPDX-FileCopyrightText: 2026 Hannes Klas
-- SPDX-License-Identifier: CC0-1.0

library ieee;
use ieee.std_logic_1164.all;

package scope_pkg is
  subtype word_t is std_logic_vector(3 downto 0);
  constant nibble_c : integer := 4;
  constant step_c : integer := 2;
  constant base_c : word_t := "0011";
  constant width_c : integer;  -- completed by the body below
  constant mask_c : word_t;  -- both deferred constants are completed by the body
  function rotate_and_mask(x : word_t) return word_t;
  function span return integer;
end package scope_pkg;

package body scope_pkg is
  -- The deferred constants above are completed here, and the specification's constants
  -- and subtype are visible in the body, which is nested in the specification.
  constant double_c : integer := nibble_c * step_c;
  constant mask_c : word_t := base_c and "1101";
  constant width_c : integer := nibble_c + step_c;

  function rotate_and_mask(x : word_t) return word_t is
    variable v : word_t := x;
  begin
    for i in 1 to step_c loop
      v := v(2 downto 0) & v(3);
    end loop;
    return v and mask_c;
  end function;

  function span return integer is
  begin
    return double_c + word_t'length;
  end function;
end package body scope_pkg;

library ieee;
use ieee.std_logic_1164.all;

library work;
use work.scope_pkg.all;

entity t is
  port (
    i_a     : in  std_ulogic;
    i_d     : in  word_t;
    o_shift : out word_t;
    o_mask  : out word_t;
    o_const : out integer;
    o_width : out integer
  );
end entity t;

architecture rtl of t is
  -- Called with a run-time argument, so the body runs in the model; and with none, so
  -- it is folded here. Both have to see the body's own constants.
  constant span_c : integer := span;
  signal masked : word_t;
begin
  masked <= rotate_and_mask(i_d);
  o_shift <= masked when i_a = '1' else rotate_and_mask(not i_d);
  o_mask <= mask_c;
  o_const <= span_c + span;
  o_width <= width_c;
end architecture rtl;
