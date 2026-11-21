-- DESCRIPTION: Verilator: VHDL a case that covers every value of its selector
--
-- This program is free software; you can redistribute it and/or modify it
-- under the terms of either the GNU Lesser General Public License Version 3
-- or the Perl Artistic License Version 2.0.
-- SPDX-FileCopyrightText: 2026 Wilson Snyder
-- SPDX-License-Identifier: LGPL-3.0-only OR Artistic-2.0

-- A case over an expression that is not a constant becomes a chain of comparisons in
-- a folded function body. Without an `others` arm the selector's own values decide
-- the result, so a case that covers them all needs no default (IEEE 1076-2008 10.9).
-- Every value below is the one GHDL reports.

library ieee;
use ieee.std_logic_1164.all;

package casepkg is
  type t_state is (IDLE, RUN, DONE);
  function encode (s : t_state) return std_logic_vector;
end package;

library ieee;
use ieee.std_logic_1164.all;

package body casepkg is
  function encode (s : t_state) return std_logic_vector is
    variable r : std_logic_vector(1 downto 0);
  begin
    case s is
      when IDLE => r := "00";
      when RUN  => r := "01";
      when DONE => r := "10";
    end case;
    return r;
  end function;
end package body;


library ieee;
use ieee.std_logic_1164.all;
use work.casepkg.all;

entity t_vhdl_case_cover is
  port (
    s : in  t_state;
    o : out std_logic_vector(1 downto 0)
  );
end entity;

architecture rtl of t_vhdl_case_cover is
begin
  o <= encode(s);
end architecture;
